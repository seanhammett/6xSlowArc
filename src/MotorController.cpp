#include "MotorController.h"

#include <Arduino.h>
#include <math.h>
#include <driver/mcpwm.h>

#include "pins.h"

namespace {
// Channel -> MCPWM placement. Six channels fill 2 units x 3 timers exactly, so
// every motor owns an independent timer (= independent frequency = speed).
//   ch 0,1,2 -> unit 0 / timer 0,1,2      ch 3,4,5 -> unit 1 / timer 0,1,2
inline mcpwm_unit_t  unitOf(uint8_t ch)  { return ch < 3 ? MCPWM_UNIT_0 : MCPWM_UNIT_1; }
inline mcpwm_timer_t timerOf(uint8_t ch) { return (mcpwm_timer_t)(ch % 3); }
inline mcpwm_io_signals_t sigOf(uint8_t ch) {
  return (mcpwm_io_signals_t)(MCPWM0A + 2 * (ch % 3));     // generator A of that timer
}
}  // namespace

bool MotorController::begin() {
  ok_ = true;
  uint8_t okCount = 0;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    // Belt-and-braces: hold EN disabled (active-LOW => HIGH) and set DIR forward
    // before the timer drives STEP, so there's no window where the driver is live.
    pinMode(PIN_EN[ch], OUTPUT);
    digitalWrite(PIN_EN[ch], HIGH);
    pinMode(PIN_DIR[ch], OUTPUT);
    digitalWrite(PIN_DIR[ch], MOTOR_DIR_FORWARD ? HIGH : LOW);

    esp_err_t e1 = mcpwm_gpio_init(unitOf(ch), sigOf(ch), PIN_STEP[ch]);

    mcpwm_config_t cfg = {};
    cfg.frequency    = MOTOR_MIN_SPEED_HZ;     // placeholder until first command
    cfg.cmpr_a       = 0.0f;
    cfg.cmpr_b       = 0.0f;
    cfg.duty_mode    = MCPWM_DUTY_MODE_0;
    cfg.counter_mode = MCPWM_UP_COUNTER;
    esp_err_t e2 = mcpwm_init(unitOf(ch), timerOf(ch), &cfg);

    if (e1 != ESP_OK || e2 != ESP_OK) {
      channelOk_[ch] = false;
      ok_ = false;                             // flag the fault for the LED...
      continue;                                // ...but keep going for the others
    }

    mcpwm_set_signal_low(unitOf(ch), timerOf(ch), MCPWM_GEN_A);   // no pulses yet
    channelOk_[ch]   = true;
    currentHz_[ch]   = 0.0f;
    targetHz_[ch]    = 0;
    commandedHz_[ch] = 0;
    ++okCount;
  }

  // Enable ONLY the channels that initialised. A single failed channel no longer
  // keeps its siblings disabled (see note in update()).
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (channelOk_[ch]) digitalWrite(PIN_EN[ch], LOW);   // driver live, idle (no STEP)
  }

  enabled_      = (okCount > 0);
  lastUpdateMs_ = millis();
  return ok_;
}

void MotorController::emitChannel(uint8_t ch, uint32_t hz) {
  mcpwm_set_frequency(unitOf(ch), timerOf(ch), hz);
  mcpwm_set_duty(unitOf(ch), timerOf(ch), MCPWM_GEN_A, 50.0f);
  mcpwm_set_duty_type(unitOf(ch), timerOf(ch), MCPWM_GEN_A, MCPWM_DUTY_MODE_0);  // re-arm after a stop
  commandedHz_[ch] = hz;
}

void MotorController::stopChannel(uint8_t ch) {
  mcpwm_set_signal_low(unitOf(ch), timerOf(ch), MCPWM_GEN_A);
  commandedHz_[ch] = 0;
}

void MotorController::setTarget(uint8_t ch, uint16_t hz) {
  if (ch >= NUM_CHANNELS) return;
  // Clamp here rather than trusting the caller: a sequence scaling a set-point
  // down can land between 1 Hz and the floor, and we never emit below MIN.
  targetHz_[ch] = motorClampHz(hz);
}

void MotorController::setTargets(const uint16_t values[NUM_CHANNELS]) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) setTarget(ch, values[ch]);
}

void MotorController::update() {
  uint32_t now = millis();
  uint32_t dt  = now - lastUpdateMs_;
  if (dt == 0) return;
  lastUpdateMs_ = now;

  // Max frequency change this tick (soft accel/decel), in Hz.
  float maxStep = (float)MOTOR_ACCEL_HZ_S * (float)dt / 1000.0f;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (!channelOk_[ch]) continue;

    uint32_t tgt = targetHz_[ch];
    float    cur = currentHz_[ch];

    if (tgt == 0) {
      // Ramp down toward MIN, then cut pulses entirely. (We never emit below MIN:
      // MCPWM's low-frequency range is uncertain there, and a sub-MIN step rate
      // isn't useful — so the floor is the clean place to switch off.)
      if (cur <= 0.0f) { if (commandedHz_[ch]) stopChannel(ch); continue; }
      cur -= maxStep;
      if (cur <= (float)MOTOR_MIN_SPEED_HZ) {
        cur = 0.0f;
        stopChannel(ch);
      } else {
        uint32_t hz = (uint32_t)lroundf(cur);
        if (hz != commandedHz_[ch]) emitChannel(ch, hz);
      }
    } else {
      // Running: start at MIN on kick-on, then ramp toward the target rate.
      if (cur < (float)MOTOR_MIN_SPEED_HZ) cur = (float)MOTOR_MIN_SPEED_HZ;
      if (cur < (float)tgt)      cur = fminf((float)tgt, cur + maxStep);
      else if (cur > (float)tgt) cur = fmaxf((float)tgt, cur - maxStep);
      uint32_t hz = (uint32_t)lroundf(cur);
      if (hz != commandedHz_[ch]) emitChannel(ch, hz);
    }

    currentHz_[ch] = cur;
  }
}

void MotorController::emergencyStop() {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (channelOk_[ch]) mcpwm_set_signal_low(unitOf(ch), timerOf(ch), MCPWM_GEN_A);
    digitalWrite(PIN_EN[ch], HIGH);          // disable driver (active-LOW), every channel
    currentHz_[ch]   = 0.0f;
    targetHz_[ch]    = 0;
    commandedHz_[ch] = 0;
  }
  enabled_ = false;
}
