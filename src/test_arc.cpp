//
// test_arc.cpp — CONCURRENCY test: all 6 stepper drivers running at once (each on
// its own MCPWM timer), the DAC boards driven continuously over I2C, AND a basic
// WiFi web server — to confirm the ESP32-S3 sustains the whole stack at once.
//
// Each motor spins at a distinct, steady speed so you can identify and check each
// one in turn while they all run. It also exercises the front-panel inputs and the
// status LED: the mode switch and sequence button (debounced via the production
// InputManager) and the on-board + off-board status pixels (production StatusLed).
// The sequence button steps the LED through every state so you can verify each
// colour on both pixels. A web page mirrors EVERYTHING — motor speeds, bulb level,
// DAC boards, mode switch, button presses, and the live LED colour — and lets you
// drive the motors and bulbs from a browser, proving the whole stack runs at once.
//
// MCPWM mapping (same as the real MotorController): channel i -> unit i/3,
// timer i%3, generator A. Six channels fill 2 units x 3 timers exactly.
//
// Built only by the `test_arc` PlatformIO environment (see platformio.ini).
//
//   pio run -e test_arc -t upload && pio device monitor -e test_arc
//   then join WiFi "SlowArcTest" (pass slowarc123) and browse http://192.168.4.1/
//

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DFRobot_GP8403.h>
#include <driver/mcpwm.h>

#include "pins.h"
#include "config.h"
#include "SystemState.h"      // LedState / Fault
#include "ChannelModel.h"     // Mode
#include "StatusLed.h"        // production status-LED driver (on-board + off-board)
#include "InputManager.h"     // production mode switch + sequence button

// Per-channel speed spread: channel i runs at BASE + i*STEP Hz (clamped to MAX),
// then scaled by the web slider. Distinct rates make each motor easy to pick out.
#ifndef TEST_BASE_HZ
#define TEST_BASE_HZ 300
#endif
#ifndef TEST_STEP_HZ
#define TEST_STEP_HZ 300
#endif

// STA credentials come from the gitignored src/secrets.h (copy secrets.h.example
// to secrets.h and fill it in). If that file is absent — e.g. a fresh clone — we
// fall back to blank creds, which means the SoftAP below. Nothing secret in git.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#ifndef SECRET_WIFI_SSID
#  define SECRET_WIFI_SSID ""
#endif
#ifndef SECRET_WIFI_PASS
#  define SECRET_WIFI_PASS ""
#endif

static const char* kStaSsid = SECRET_WIFI_SSID;   // set in src/secrets.h
static const char* kStaPass = SECRET_WIFI_PASS;

static const char* kApSsid = "SlowArcTest";       // fallback access point
static const char* kApPass = "slowarc123";        // >= 8 chars

static const uint32_t kControlMs = 40;       // ramp/output update period
static const uint32_t kPrintMs   = 1000;     // console print period

// --- MCPWM placement helpers ----------------------------------------------
namespace {
inline mcpwm_unit_t  unitOf(uint8_t ch)  { return ch < 3 ? MCPWM_UNIT_0 : MCPWM_UNIT_1; }
inline mcpwm_timer_t timerOf(uint8_t ch) { return (mcpwm_timer_t)(ch % 3); }
inline mcpwm_io_signals_t sigOf(uint8_t ch) {
  return (mcpwm_io_signals_t)(MCPWM0A + 2 * (ch % 3));
}
}  // namespace

static void stepEmit(uint8_t ch, uint32_t hz) {
  mcpwm_set_frequency(unitOf(ch), timerOf(ch), hz);
  mcpwm_set_duty(unitOf(ch), timerOf(ch), MCPWM_GEN_A, 50.0f);
  mcpwm_set_duty_type(unitOf(ch), timerOf(ch), MCPWM_GEN_A, MCPWM_DUTY_MODE_0);
}

// --- Hardware / shared state ----------------------------------------------
static DFRobot_GP8403* dac_[3]     = {nullptr, nullptr, nullptr};
static bool            boardOk_[3] = {false, false, false};

static bool     motorOk_[NUM_CHANNELS]   = {false};
static uint32_t baseHz_[NUM_CHANNELS]    = {0};   // distinct per-channel rate
static float    curHz_[NUM_CHANNELS]     = {0};   // ramped actual
static uint32_t appliedHz_[NUM_CHANNELS] = {0};   // last freq written to MCPWM

static float    speedScale_ = 1.0f;               // 0..1, motor speed (web slider)
static uint8_t  bulbPct_    = 50;                  // 0..100, bulb level (web slider)
static uint16_t bulbMv_     = 0;                   // last DAC value (for the page)
static bool     apMode_     = false;               // true if we fell back to SoftAP

// Status LED + front-panel inputs (production classes). The sequence button steps
// the status LED through every state so you can verify each colour on BOTH the
// on-board (GPIO48) and off-board (GPIO40) pixels; the mode switch is reported.
static StatusLed    statusLed;
static InputManager inputs;

struct LedEntry { LedState state; Fault fault; const char* name; };
static const LedEntry kLedCycle[] = {
  { LedState::Boot,           Fault::None,    "Boot — solid amber" },
  { LedState::GalleryHealthy, Fault::None,    "Gallery — green pulse" },
  { LedState::PerformanceRun, Fault::None,    "Performance — blue pulse" },
  { LedState::WifiConnecting, Fault::None,    "WiFi — amber pulse" },
  { LedState::Ota,            Fault::None,    "OTA — fast blue" },
  { LedState::Fault,          Fault::BulbDac, "Fault 2 — DAC (2 blinks)" },
  { LedState::Fault,          Fault::Motor,   "Fault 3 — motor (3 blinks)" },
  { LedState::Fault,          Fault::Supply,  "Fault 4 — supply (4 blinks)" },
};
static const uint8_t kLedCycleN = sizeof(kLedCycle) / sizeof(kLedCycle[0]);
static uint8_t  ledIdx_   = 0;
static uint32_t seqCount_ = 0;
static Mode     mode_     = Mode::Gallery;

static WebServer server(80);

static void i2cScan() {
  Serial.println("[i2c] scanning...");
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("[i2c]   found 0x%02X\n", a);
  }
}

// --- Web handlers ----------------------------------------------------------
static const char kPage[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Slow Arc — test rig</title>
<style>
 body{font-family:system-ui,sans-serif;background:#15171c;color:#e8e8e8;margin:0;padding:1rem;max-width:560px;margin:auto}
 h1{font-size:1.2rem} .card{background:#1e2128;border-radius:8px;padding:.8rem 1rem;margin:.6rem 0}
 .m{display:flex;justify-content:space-between;font-variant-numeric:tabular-nums;padding:.15rem 0}
 input[type=range]{width:100%} output{font-weight:bold;color:#9ad}
</style></head><body>
<h1>Slow Arc — concurrency test</h1>
<div class="card">
 <label>Motor speed scale: <output id="sv">100</output>%</label>
 <input type="range" id="sc" min="0" max="100" value="100"
   oninput="sv.textContent=this.value;fetch('/set?scale='+this.value)">
</div>
<div class="card">
 <label>Bulb level: <output id="bv">50</output>%</label>
 <input type="range" id="bl" min="0" max="100" value="50"
   oninput="bv.textContent=this.value;fetch('/set?bulb='+this.value)">
</div>
<div class="card" id="io">inputs…</div>
<div class="card" id="mot">motors…</div>
<div class="card" id="bulb">bulb…</div>
<script>
async function poll(){
 try{const d=await (await fetch('/status')).json();
  document.getElementById('io').innerHTML='<b>Inputs &amp; status LED</b>'+
    `<div class="m"><span>Mode switch</span><span>${d.mode}</span></div>`+
    `<div class="m"><span>Seq presses</span><span>${d.seq}</span></div>`+
    `<div class="m"><span>Status LED</span><span>`+
      `<span style="display:inline-block;width:.8rem;height:.8rem;border-radius:50%;`+
      `border:1px solid #444;background:${d.led_rgb};vertical-align:middle;margin-right:.4rem"></span>`+
      `${d.led}</span></div>`;
  document.getElementById('mot').innerHTML='<b>Motors (Hz)</b>'+
    d.motors.map((h,i)=>`<div class="m"><span>ch${i}</span><span>${h<0?'fault':h}</span></div>`).join('');
  document.getElementById('bulb').innerHTML=
    `<b>Bulbs</b><div class="m"><span>DAC out</span><span>${d.bulb_mv} mV</span></div>`+
    `<div class="m"><span>Boards</span><span>${d.boards.join(' ')||'none'}</span></div>`;
 }catch(e){}
}
setInterval(poll,1000); poll();
</script></body></html>
)HTML";

static void handleRoot()   { server.send_P(200, "text/html", kPage); }

static void handleStatus() {
  String j = "{\"scale\":";
  j += (int)lroundf(speedScale_ * 100);
  j += ",\"bulb_mv\":";
  j += bulbMv_;
  j += ",\"motors\":[";
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (ch) j += ',';
    j += motorOk_[ch] ? (long)appliedHz_[ch] : -1;   // -1 => fault
  }
  j += "],\"boards\":[";
  bool first = true;
  for (uint8_t b = 0; b < 3; ++b) {
    if (!boardOk_[b]) continue;
    if (!first) j += ',';
    char buf[8]; snprintf(buf, sizeof(buf), "\"0x%02X\"", DAC_ADDRESSES[b]);
    j += buf; first = false;
  }
  j += "],\"mode\":\"";
  j += (mode_ == Mode::Performance) ? "Performance" : "Gallery";
  j += "\",\"seq\":";
  j += seqCount_;
  j += ",\"led\":\"";
  j += kLedCycle[ledIdx_].name;
  j += "\",\"led_rgb\":\"";
  char col[8]; snprintf(col, sizeof(col), "#%06X", (unsigned)statusLed.rgb());
  j += col;
  j += "\"}";
  server.send(200, "application/json", j);
}

static void handleSet() {
  if (server.hasArg("scale")) {
    int v = server.arg("scale").toInt();
    if (v < 0) v = 0; if (v > 100) v = 100;
    speedScale_ = v / 100.0f;
  }
  if (server.hasArg("bulb")) {
    int v = server.arg("bulb").toInt();
    if (v < 0) v = 0; if (v > 100) v = 100;
    bulbPct_ = (uint8_t)v;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// --- Lifecycle -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ALL 6 motors + DACs + web + LED/inputs test ===");

  // --- Front-panel inputs + status LED (on-board + off-board) ---
  inputs.begin();
  statusLed.begin();
  statusLed.set(kLedCycle[ledIdx_].state, kLedCycle[ledIdx_].fault);
  statusLed.update();

  // --- I2C + DAC boards ---
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  i2cScan();
  for (uint8_t b = 0; b < 3; ++b) {
    dac_[b] = new DFRobot_GP8403(&Wire, DAC_ADDRESSES[b]);
    if (dac_[b]->begin() == 0) {
      dac_[b]->setDACOutRange(DFRobot_GP8403::eOutputRange10V);
      dac_[b]->setDACOutVoltage(0, 2);
      boardOk_[b] = true;
    }
    Serial.printf("[dac] board %u (0x%02X): %s\n",
                  b, DAC_ADDRESSES[b], boardOk_[b] ? "OK" : "absent");
  }

  // --- All 6 MCPWM motor channels ---
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    pinMode(PIN_EN[ch], OUTPUT);
    digitalWrite(PIN_EN[ch], HIGH);
    pinMode(PIN_DIR[ch], OUTPUT);
    digitalWrite(PIN_DIR[ch], MOTOR_DIR_FORWARD ? HIGH : LOW);

    esp_err_t e1 = mcpwm_gpio_init(unitOf(ch), sigOf(ch), PIN_STEP[ch]);
    mcpwm_config_t cfg = {};
    cfg.frequency    = MOTOR_MIN_SPEED_HZ;
    cfg.cmpr_a       = 0.0f;
    cfg.cmpr_b       = 0.0f;
    cfg.duty_mode    = MCPWM_DUTY_MODE_0;
    cfg.counter_mode = MCPWM_UP_COUNTER;
    esp_err_t e2 = mcpwm_init(unitOf(ch), timerOf(ch), &cfg);

    motorOk_[ch] = (e1 == ESP_OK && e2 == ESP_OK);
    if (!motorOk_[ch]) { Serial.printf("[mot] ch%u MCPWM init FAILED\n", ch); continue; }

    mcpwm_set_signal_low(unitOf(ch), timerOf(ch), MCPWM_GEN_A);
    uint32_t hz = TEST_BASE_HZ + (uint32_t)ch * TEST_STEP_HZ;
    if (hz > MOTOR_MAX_SPEED_HZ) hz = MOTOR_MAX_SPEED_HZ;
    baseHz_[ch] = hz;
  }

  Serial.println("[mot] target speeds (each motor a distinct rate):");
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (!motorOk_[ch]) continue;
    digitalWrite(PIN_EN[ch], LOW);
    Serial.printf("[mot]   ch%u  STEP=%u  unit %d/timer %d  -> %lu Hz\n",
                  ch, PIN_STEP[ch], (int)unitOf(ch), (int)timerOf(ch),
                  (unsigned long)baseHz_[ch]);
  }

  // --- WiFi: join the configured network, else fall back to a SoftAP ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(kStaSsid, kStaPass);
  Serial.printf("[web] joining '%s'", kStaSsid);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(250);
    Serial.print('.');
  }
  String ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
    Serial.printf(" connected\n");
  } else {
    apMode_ = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    ip = WiFi.softAPIP().toString();
    Serial.printf(" FAILED — SoftAP '%s' (pass %s)\n", kApSsid, kApPass);
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.begin();
  Serial.printf("[web] open  http://%s/\n", ip.c_str());

  Serial.println("\nMotors run together (scale from the web); bulbs breathe (I2C busy).\n");
}

void loop() {
  server.handleClient();                         // every iteration, keep web responsive

  uint32_t now = millis();

  // --- Front-panel inputs + status-LED animation (~20 ms, independent of the
  // motor tick). Each sequence-button press steps the LED to the next state so you
  // can verify every colour on both the on-board and off-board pixels. ---
  static uint32_t lastIo = 0;
  if (now - lastIo >= 20) {
    lastIo = now;
    inputs.update();
    mode_ = inputs.mode();
    if (inputs.sequencePressed()) {
      seqCount_++;
      ledIdx_ = (ledIdx_ + 1) % kLedCycleN;
    }
    statusLed.set(kLedCycle[ledIdx_].state, kLedCycle[ledIdx_].fault);
    statusLed.update();                          // drives both pixels in lockstep
  }

  // --- Motor / DAC control tick ---
  static uint32_t lastCtrl = 0, lastPrint = 0;
  if (now - lastCtrl < kControlMs) return;
  uint32_t dt = now - lastCtrl;
  lastCtrl = now;

  // --- Bulbs: drive every present DAC channel to the web-set level (written
  // every tick, so the I2C bus stays exercised even when the level is steady) ---
  bulbMv_ = (uint16_t)lroundf((float)bulbPct_ / 100.0f * DAC_FULLSCALE_MV);
  for (uint8_t b = 0; b < 3; ++b) {
    if (boardOk_[b]) dac_[b]->setDACOutVoltage(bulbMv_, 2);
  }

  // --- Motors: ramp each channel toward its (scaled) distinct target ---
  float maxStep = (float)MOTOR_ACCEL_HZ_S * (float)dt / 1000.0f;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (!motorOk_[ch]) continue;

    // Effective target = distinct base rate scaled by the web slider. Below MIN we
    // stop rather than emit an unusable sub-MIN frequency.
    uint32_t tgt = (uint32_t)lroundf((float)baseHz_[ch] * speedScale_);
    if (tgt != 0 && tgt < MOTOR_MIN_SPEED_HZ) tgt = MOTOR_MIN_SPEED_HZ;

    float cur = curHz_[ch];
    if (tgt == 0) {
      cur -= maxStep;
      if (cur <= (float)MOTOR_MIN_SPEED_HZ) {
        cur = 0.0f;
        if (appliedHz_[ch] != 0) {
          mcpwm_set_signal_low(unitOf(ch), timerOf(ch), MCPWM_GEN_A);
          appliedHz_[ch] = 0;
        }
        curHz_[ch] = cur;
        continue;
      }
    } else {
      if (cur < (float)MOTOR_MIN_SPEED_HZ) cur = (float)MOTOR_MIN_SPEED_HZ;   // kick-on
      if (cur < (float)tgt)      cur = fminf((float)tgt, cur + maxStep);
      else if (cur > (float)tgt) cur = fmaxf((float)tgt, cur - maxStep);
    }

    uint32_t hz = (uint32_t)lroundf(cur);
    if (hz != appliedHz_[ch]) { stepEmit(ch, hz); appliedHz_[ch] = hz; }
    curHz_[ch] = cur;
  }

  // --- Console status line ---
  if (now - lastPrint >= kPrintMs) {
    lastPrint = now;
    Serial.printf("[run] scale=%3d%% motors Hz:", (int)lroundf(speedScale_ * 100));
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
      if (motorOk_[ch]) Serial.printf(" ch%u=%4lu", ch, (unsigned long)appliedHz_[ch]);
      else              Serial.printf(" ch%u=----", ch);
    }
    if (apMode_) Serial.printf("  | bulbs=%4u mV  ap-clients=%d\n", bulbMv_, WiFi.softAPgetStationNum());
    else         Serial.printf("  | bulbs=%4u mV  rssi=%d dBm\n",    bulbMv_, WiFi.RSSI());
    Serial.printf("[io ] mode=%-11s seq=%lu  led=%s\n",
                  mode_ == Mode::Performance ? "Performance" : "Gallery",
                  (unsigned long)seqCount_, kLedCycle[ledIdx_].name);
  }
}
