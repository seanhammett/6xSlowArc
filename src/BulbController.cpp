#include "BulbController.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <DFRobot_GP8403.h>

#include "pins.h"

// Map an Arc channel (0..5) to a DAC board (0..2) and that board's channel (0/1).
//   Arc 0 -> board0/ch0   Arc 1 -> board0/ch1
//   Arc 2 -> board1/ch0   Arc 3 -> board1/ch1   ... etc.
static inline uint8_t boardOf(uint8_t ch)    { return ch / 2; }
static inline uint8_t boardChan(uint8_t ch)  { return ch % 2; }

bool BulbController::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  ok_ = true;
  for (uint8_t b = 0; b < 3; ++b) {
    dac_[b] = new DFRobot_GP8403(&Wire, DAC_ADDRESSES[b]);
    if (dac_[b]->begin() != 0) {
      ok_ = false;                    // board didn't ACK
      boardOk_[b] = false;
      continue;
    }
    dac_[b]->setDACOutRange(DFRobot_GP8403::eOutputRange10V);  // sets full-scale
    boardOk_[b] = true;
  }

  // Force every channel dark and arm the boot stagger so the first ramp-up is
  // spread across the channels.
  uint32_t now = millis();
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    current_[ch]   = 0.0f;
    target_[ch]    = 0;
    holdFromMs_[ch] = now;
    holdMs_[ch]     = (uint32_t)ch * BULB_STAGGER_MS;
    writeChannel(ch, 0);
  }
  lastUpdateMs_ = now;
  return ok_;
}

uint16_t BulbController::levelToMilliVolts(float level0to255) const {
  if (level0to255 <= 0.0f) return 0;
  float norm = level0to255 / 255.0f;            // 0..1, quantised to 256 levels
  // Straight linear map: the DIM14 dimmer applies its own perceptual (gamma)
  // curve, so the firmware must NOT gamma-correct as well or the two compound and
  // crush the low end.
  return (uint16_t)lroundf(norm * DAC_FULLSCALE_MV);
}

void BulbController::writeChannel(uint8_t ch, uint16_t milliVolts) {
  uint8_t b = boardOf(ch);
  if (!boardOk_[b] || !dac_[b]) return;   // skip boards that never initialised
  dac_[b]->setDACOutVoltage(milliVolts, boardChan(ch));
}

void BulbController::setTarget(uint8_t ch, uint8_t value) {
  if (ch >= NUM_CHANNELS) return;

  // A large increase from a low/idle level gets re-staggered against any other
  // channels already waiting to ramp up, so a burst of "turn everything up"
  // writes from the web UI can't inrush together.
  if (value > target_[ch] && (value - (uint8_t)current_[ch]) > BULB_LARGE_STEP) {
    uint32_t now = millis();
    uint8_t pending = 0;
    for (uint8_t c = 0; c < NUM_CHANNELS; ++c) {
      if (c == ch) continue;
      bool risingOrHeld = (current_[c] < target_[c]) || held(c, now);
      if (risingOrHeld) ++pending;
    }
    holdFromMs_[ch] = now;
    holdMs_[ch]     = (uint32_t)pending * BULB_STAGGER_MS;
  }
  target_[ch] = value;
}

void BulbController::setTargets(const uint8_t values[NUM_CHANNELS]) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) setTarget(ch, values[ch]);
}

void BulbController::update() {
  uint32_t now = millis();
  uint32_t dt  = now - lastUpdateMs_;
  if (dt == 0) return;
  lastUpdateMs_ = now;

  // Max change this tick, in 0..255 units, given the full-range ramp time.
  float maxStep = 255.0f * (float)dt / (float)BULB_RAMP_MS;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    float tgt = (float)target_[ch];
    if (current_[ch] < tgt) {
      if (held(ch, now)) continue;               // staggered hold-off
      current_[ch] = min(tgt, current_[ch] + maxStep);
    } else if (current_[ch] > tgt) {
      current_[ch] = max(tgt, current_[ch] - maxStep);
    } else {
      continue;                                  // already at target
    }
    writeChannel(ch, levelToMilliVolts(current_[ch]));
  }
}

void BulbController::allOff() {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    target_[ch]    = 0;
    holdMs_[ch]    = 0;
  }
  // Leave update() to ramp them down smoothly; callers that need an instant kill
  // lose the rail anyway.
}
