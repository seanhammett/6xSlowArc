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
// motor, 4 = supply/brownout).
//

#include <Arduino.h>
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
// brownout is surfaced for a short window after boot.
static Fault currentFault() {
  if (hwFault_ != Fault::None) return hwFault_;
  if (brownoutReported_ && millis() < brownoutClearMs_) return Fault::Supply;
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
  j += (inputs.mode() == Mode::Performance) ? "Performance" : "Gallery";
  j += "\",";
  j += "\"running\":"; j += sequence.running() ? "true" : "false"; j += ',';
  j += "\"seq_t\":"; j += sequence.positionMs(); j += ',';
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
// next button push while running. Fired by the web UI on select/save-active.
static void onSequenceActivated(uint8_t slot) {
  SeqDef def;
  if (seqStore.load(slot, def)) sequence.apply(def);
}

// The choreography table for the web UI's timeline view (fetched once).
// Values are scales of the set-points (255 = the set-point).
static String buildSequenceJson() {
  String j;
  j.reserve(1024);
  j += "{\"len\":"; j += sequence.loopLengthMs(); j += ",\"cues\":[";
  for (uint16_t i = 0; i < sequence.cueCount(); ++i) {
    const Cue& c = sequence.cues()[i];
    if (i) j += ',';
    j += "{\"t\":"; j += c.timeMs;
    j += ",\"b\":[";
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) { if (ch) j += ','; j += c.brightness[ch]; }
    j += "],\"s\":[";
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) { if (ch) j += ','; j += c.speed[ch]; }
    j += "]}";
  }
  j += "]}";
  return j;
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

  webUi.begin(&model, &configStore, &seqStore, arcOn_, buildStateJson, buildSequenceJson,
              [](const String& ssid, const String& pass) {
                network.setCredentials(ssid, pass);   // NVS + immediate attempt
              },
              onSequenceActivated);

  watchdogBegin();
}

// -------------------------------------------------------------------------
void loop() {
  inputs.update();

  // Mode follows the physical switch; persist it when it changes so status/boot
  // reporting stays in sync (the switch is authoritative for live operation).
  Mode mode = inputs.mode();
  if (mode != model.mode) {
    model.mode = mode;
    configStore.markDirty();
  }

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

  // Status LED: fault > OTA > WiFi-connecting > operational.
  Fault f = currentFault();
  if (f != Fault::None) {
    statusLed.set(LedState::Fault, f);
  } else if (ota.inProgress()) {
    statusLed.set(LedState::Ota);
  } else if (network.connecting()) {
    statusLed.set(LedState::WifiConnecting);
  } else if (mode == Mode::Performance) {
    // Blue the moment the switch moves, so the operator gets feedback from the
    // switch itself rather than only from the arcs once a piece is under way.
    statusLed.set(sequence.running() ? LedState::PerformanceRun
                                     : LedState::PerformanceIdle);
  } else {
    statusLed.set(LedState::GalleryHealthy);
  }
  statusLed.update();

  // Networking + remote services (all non-blocking).
  network.tick();
  ota.setEnabled(network.online());
  ota.handle();

  // Debounced NVS commit.
  configStore.tick();

  // Re-subscribe to the watchdog once an aborted OTA hands control back.
  if (!ota.inProgress() && !wdtSubscribed_) {
    esp_task_wdt_add(NULL);
    wdtSubscribed_ = true;
  }
  if (wdtSubscribed_) esp_task_wdt_reset();
}
