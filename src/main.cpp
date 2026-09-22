//
// Slow Arc Controller — firmware entry point.
//
// One ESP32-S3 box driving six kinetic light artworks. Each "Slow Arc" is a
// dimmable halogen bulb-pair (via DAC -> DIM14) plus a geared stepper (TMC2209).
// See slow_arc_controller_spec.md for the full build spec.
//
// This file owns the wiring between modules and the main control loop. It is the
// only place that sees every subsystem, so it also assembles the web status
// snapshot and decides the status-LED state.
//
// PERSISTENCE MODEL (spec §6/§8): NVS is the single source of persisted state.
// The 12 set-points (6 brightness + 6 speed) and the mode live in ChannelModel
// and are restored on boot by ConfigStore; web-UI writes are coalesced and
// committed back to NVS. With WiFi down the box runs entirely from this stored
// configuration — the network is only needed to change settings or do OTA.
//
// STATUS-LED CODES (spec §7): solid amber = boot/soft-start; slow green pulse =
// gallery healthy; fast blue blink = performance armed; slow blue pulse =
// performance running; amber pulse = WiFi connecting; fast blue pulse = OTA;
// red blink = fault, blink count encodes the subsystem (2 = bulb/DAC, 3 =
// motor, 4 = supply/brownout). A short white blip every 2 s is laid over any of
// these while the web has overridden the mode switch.
//
// MODE + PLAYBACK OWNERSHIP: model.mode is the box's mode. The front-panel switch
// and the web page both change it and the last change wins — the switch acts on
// its flip, not its position — so a box mounted out of reach is fully driven
// from the web. Boot restores the saved mode, stopped; nothing auto-plays.
//

#include <Arduino.h>
#include <atomic>
#include <new>
#include <esp_task_wdt.h>
#include <esp_system.h>

#include "config.h"
#include "ChannelModel.h"
#include "ConfigStore.h"
#include "BulbController.h"
#include "MotorController.h"
#include "InputManager.h"
#include "SequenceEngine.h"
#include "SequenceStore.h"
#include "StatusLed.h"
#include "NetworkSupervisor.h"
#include "OtaService.h"
#include "WebUi.h"
#include "SystemState.h"

// --- Modules --------------------------------------------------------------
static ChannelModel    model;
static ConfigStore     configStore;
static BulbController   bulbs;
static MotorController  motors;
static InputManager     inputs;
static SequenceEngine   sequence;
static SequenceStore    seqStore;
static StatusLed        statusLed;
static NetworkSupervisor   network;
static OtaService       ota;
static WebUi            webUi;

// --- Cross-cutting state --------------------------------------------------
static Fault    hwFault_ = Fault::None;     // sticky hardware-init fault
static bool     brownoutReported_ = false;  // last reset attributed to the rail
static uint32_t brownoutClearMs_ = 0;       // show the supply fault until here
static bool     wdtSubscribed_ = false;

// Effective per-channel targets actually pushed to the controllers. In Gallery
// these track the static set-points; in Performance they come from the sequence.
static uint8_t  effBrightness_[NUM_CHANNELS];
static uint16_t effSpeedHz_[NUM_CHANNELS];

// Per-arc web kill switches. Runtime only — never persisted, boots all-on. A
// disabled arc is forced dark/stopped whatever the mode; set-points are untouched.
static bool arcOn_[NUM_CHANNELS] = { true, true, true, true, true, true };

// Mode / playback commands from the web page. The handlers run on the async web
// task, so they only post here; loop() consumes them and alone touches the
// engine. -1 = nothing pending.
static std::atomic<int8_t> webMode_{-1};    // (int8_t)Mode
static std::atomic<int8_t> webRun_{-1};     // 0 = stop, 1 = play
static std::atomic<int8_t> webSeqSlot_{-1}; // stored slot to hand to the engine
static std::atomic<int32_t> webNudgeMs_{0}; // summed audio-follow nudges, 0 = none

// -------------------------------------------------------------------------
// Watchdog. A hang resets the chip; the EN pull-ups then hold the motors
// disabled (safe state) and the DAC simply stops being driven.
// -------------------------------------------------------------------------
static void watchdogBegin() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_deinit();          // core may have inited one already
  esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);
  wdtSubscribed_ = true;
}

// The current fault to present: a real hardware fault wins; otherwise a recent
// brownout is surfaced for a short window after boot. The flag is dropped once
// the window has passed, or millis() wrapping (49.7 days) would raise it again.
static Fault currentFault() {
  if (hwFault_ != Fault::None) return hwFault_;
  if (brownoutReported_) {
    if (millis() < brownoutClearMs_) return Fault::Supply;
    brownoutReported_ = false;
  }
  return Fault::None;
}

// Quote a string for JSON. Sequence names and SSIDs are operator-supplied, and
// one stray quote would break the whole snapshot — which the page can only
// report as "offline".
static String jsonEscape(const char* s) {
  String o;
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') o += '\\';
    o += *s;
  }
  return o;
}

// JSON snapshot for the web UI. Assembled here because only main sees it all.
static String buildStateJson() {
  String j;
  j.reserve(512);
  j += '{';

  j += "\"brightness\":[";
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) { if (i) j += ','; j += model.brightness[i]; }
  j += "],\"speed\":[";                       // step rate in Hz, not a 0..255 scale
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) { if (i) j += ','; j += model.speedHz[i]; }
  j += "],";

  j += "\"on\":[";
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) { if (i) j += ','; j += arcOn_[i] ? 1 : 0; }
  j += "],";

  j += "\"mode\":\"";
  j += (model.mode == Mode::Performance) ? "Performance" : "Gallery";
  j += "\",";
  // The switch says something other than the box is doing (web overrode it).
  j += "\"override\":"; j += (inputs.mode() != model.mode) ? "true" : "false"; j += ',';
  j += "\"running\":"; j += sequence.running() ? "true" : "false"; j += ',';
  j += "\"seq_t\":"; j += sequence.positionMs(); j += ',';
  // Loop length + a counter that bumps on every start: the page's audio follows
  // the box's clock with these (and tells a restart from ordinary progress).
  j += "\"seq_len\":"; j += sequence.loopLengthMs(); j += ',';
  j += "\"run_id\":"; j += sequence.runId(); j += ',';
  // Which piece is loaded, and which one is waiting for the next button push.
  // The UI names the timeline from these and refetches the cue table whenever
  // seq_name changes, so a selection made on another phone still shows up.
  j += "\"seq_name\":\""; j += jsonEscape(sequence.name()); j += "\",";
  j += "\"seq_queued\":\""; j += jsonEscape(sequence.queuedName()); j += "\",";

  const char* wifi = "offline";
  switch (network.status()) {
    case NetworkSupervisor::Status::Connecting:        wifi = "connecting"; break;
    case NetworkSupervisor::Status::StationConnected:  wifi = "station";    break;
    case NetworkSupervisor::Status::AccessPoint:       wifi = "ap";         break;
    default:                                        wifi = "offline";    break;
  }
  j += "\"wifi\":\""; j += wifi; j += "\",";
  j += "\"ssid\":\""; j += jsonEscape(network.ssid().c_str()); j += "\",";
  j += "\"ip\":\""; j += network.ipAddress(); j += "\",";
  // mDNS name for the status line, so the box is findable without noting its
  // DHCP address. It only routes on the house network (mDNS comes up with the
  // OTA service), so report it empty unless we're a station.
  j += "\"host\":\"";
  if (network.status() == NetworkSupervisor::Status::StationConnected) {
    j += OTA_HOSTNAME; j += ".local";
  }
  j += "\",";

  j += "\"fault\":"; j += (int)currentFault(); j += ',';
  j += "\"uptime\":"; j += (millis() / 1000);
  j += '}';
  return j;
}

// Hand a stored sequence to the engine: live when stopped, or queued for the
// next button push while running. Loop task only — the web UI's select /
// save-active posts the slot to webSeqSlot_ and loop() calls this.
static void onSequenceActivated(uint8_t slot) {
  static SeqDef def;                          // ~8 KB: static, never on the stack
  if (seqStore.load(slot, def)) sequence.apply(def);
}

// The piece the engine is playing, for the web UI's timeline (fetched when the
// loaded piece changes): {"name","ramp_ms","len","steps":[[t_ms,mask],...]}.
// Runs on the web task, so it serialises a snapshot rather than the live piece.
static void writeSequenceJson(Print& out) {
  SeqDef* d = new (std::nothrow) SeqDef;      // ~8 KB: heap, not the web task's stack
  if (!d) { out.print("{}"); return; }
  sequence.snapshot(*d);
  printSeqJson(out, *d);
  delete d;
}

// Change the box's mode, persisting only a real change (debounced NVS commit).
static void setMode(Mode m) {
  if (m == model.mode) return;
  model.mode = m;
  configStore.markDirty();
}

// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Why did we (re)start? A brownout points at the bulb-inrush rail sag the spec
  // warns about (§8) — report it so the installer adds bulk capacitance.
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_BROWNOUT) {
    brownoutReported_ = true;
    brownoutClearMs_  = millis() + 15000;     // surface for 15s, then clear
    Serial.println("[boot] reset reason: BROWNOUT — check logic-supply bulk cap");
  }

  statusLed.begin();
  statusLed.set(LedState::Boot);              // solid amber through init/soft-start
  statusLed.update();

  inputs.begin();

  // Restore persisted set-points + mode (or first-boot defaults).
  configStore.begin(model);

  // Bring up the hardware. Failures are non-fatal: a missing DAC shouldn't stop
  // the motors and vice-versa. We flag the worst fault on the LED.
  if (!bulbs.begin())  hwFault_ = Fault::BulbDac;
  if (!motors.begin()) hwFault_ = Fault::Motor;   // motor fault outranks bulb here

  sequence.begin();
  seqStore.begin();                           // seeds the built-in piece on first boot
  onSequenceActivated(seqStore.activeIndex());  // arm the engine with the active one

  // Seed effective targets from the restored set-points so the soft-start ramps
  // straight to the commissioned values.
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
    effBrightness_[i] = model.brightness[i];
    effSpeedHz_[i]    = model.speedHz[i];
  }

  network.begin();
  ota.begin([]() {                            // safe-state the rig before flashing
    motors.emergencyStop();
    bulbs.allOff();
    if (wdtSubscribed_) { esp_task_wdt_delete(NULL); wdtSubscribed_ = false; }
  });

  webUi.begin(&model, &configStore, &seqStore, arcOn_, buildStateJson, writeSequenceJson,
              [](const String& ssid, const String& pass) {
                network.setCredentials(ssid, pass);   // NVS + immediate attempt
              },
              [](uint8_t slot) { webSeqSlot_ = (int8_t)slot; },
              [](Mode m) { webMode_ = (int8_t)m; },
              [](bool on) { webRun_ = on ? 1 : 0; },
              [](int32_t ms, uint32_t run) {
                if (run == sequence.runId()) webNudgeMs_ += ms;
              });

  watchdogBegin();
}

// -------------------------------------------------------------------------
void loop() {
  inputs.update();

  // Mode: last change wins between a switch flip and the web page.
  if (inputs.modeChanged()) setMode(inputs.mode());
  int8_t wm = webMode_.exchange(-1);
  if (wm >= 0) setMode(wm == (int8_t)Mode::Performance ? Mode::Performance : Mode::Gallery);

  // A sequence selected or re-saved on the web. Before playback, so a selection
  // made just ahead of Play is the piece that starts.
  int8_t ws = webSeqSlot_.exchange(-1);
  if (ws >= 0) onSequenceActivated((uint8_t)ws);

  // Audio follow: a browser nudging the sequence clock toward its track. Before
  // playback, so a nudge queued for the old run is spent before any restart.
  int32_t wn = webNudgeMs_.exchange(0);
  if (wn != 0) sequence.nudge(wn);

  // Web playback. Play implies Performance, so it works from Gallery in one tap,
  // and is a no-op while already playing so a double tap can't restart the piece.
  int8_t wr = webRun_.exchange(-1);
  if (wr == 1) {
    setMode(Mode::Performance);
    if (!sequence.running()) sequence.start();
  } else if (wr == 0 && sequence.running()) {
    sequence.stop();
  }

  Mode mode = model.mode;

  // Decide the effective targets for this tick.
  if (mode == Mode::Performance) {
    if (inputs.sequencePressed()) sequence.toggle();
    if (sequence.running()) {
      sequence.fill(model.brightness, model.speedHz, effBrightness_, effSpeedHz_);
    } else if (!SEQUENCE_STOP_HOLD) {
      for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
        effBrightness_[i] = model.brightness[i];
        effSpeedHz_[i]    = model.speedHz[i];
      }
    }
    // else: hold the last produced frame (SEQUENCE_STOP_HOLD) — leave eff as-is.
  } else {                                    // Gallery: clean handback to set-points
    if (sequence.running()) sequence.stop();
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      effBrightness_[i] = model.brightness[i];
      effSpeedHz_[i]    = model.speedHz[i];
    }
  }

  // Per-arc kill switches: force disabled arcs dark/stopped whatever the mode
  // decided; the controllers' ramps make the transition gentle. Masked into
  // copies so the held eff frame survives an off/on cycle (SEQUENCE_STOP_HOLD).
  uint8_t  outBrightness[NUM_CHANNELS];
  uint16_t outSpeedHz[NUM_CHANNELS];
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
    outBrightness[i] = arcOn_[i] ? effBrightness_[i] : 0;
    outSpeedHz[i]    = arcOn_[i] ? effSpeedHz_[i]    : 0;
  }

  // Push targets and pump the ramps.
  bulbs.setTargets(outBrightness);
  motors.setTargets(outSpeedHz);
  bulbs.update();
  motors.update();

  // Status LED: fault > OTA > performance > WiFi-connecting > gallery. Performance
  // outranks WiFi so a venue without the stored network (a join attempt every
  // 45 s) can't hide armed/running from the operator; the web page shows WiFi.
  Fault f = currentFault();
  if (f != Fault::None) {
    statusLed.set(LedState::Fault, f);
  } else if (ota.inProgress()) {
    statusLed.set(LedState::Ota);
  } else if (mode == Mode::Performance) {
    // Blue the moment the mode changes (switch or web), so the operator gets
    // feedback at once rather than only from the arcs once a piece is under way.
    statusLed.set(sequence.running() ? LedState::PerformanceRun
                                     : LedState::PerformanceIdle);
  } else if (network.connecting()) {
    statusLed.set(LedState::WifiConnecting);
  } else {
    statusLed.set(LedState::GalleryHealthy);
  }
  statusLed.setOverride(inputs.mode() != mode);
  statusLed.update();

  // Networking + remote services (all non-blocking).
  network.tick();
  ota.setEnabled(network.online());
  ota.handle();

  // Debounced NVS commit.
  configStore.tick();

  // An aborted OTA hands control back: re-subscribe to the watchdog and undo the
  // safe-state onStart put the drivers in (a successful OTA reboots instead).
  if (!ota.inProgress() && !wdtSubscribed_) {
    motors.enable();
    esp_task_wdt_add(NULL);
    wdtSubscribed_ = true;
  }
  if (wdtSubscribed_) esp_task_wdt_reset();
}
