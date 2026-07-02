#pragma once
//
// BulbController — owns the three GP8403 DACs and drives the six DIM14 dimmers.
//
// Responsibilities (spec §4, §8):
//   * Take a per-channel brightness set-point (0..255) and map it LINEARLY to the
//     0..10V DAC output. Perceptual (gamma) correction is done downstream by the
//     DIM14 dimmer, so the firmware deliberately does not gamma-correct.
//   * Soft-start: rate-limit every change so the 0..10V control never steps.
//   * Stagger: offset channels' ramp-up so twelve cold filaments don't inrush
//     together on the one 1000W rail.
//
// It never PWMs the bulbs — it writes a voltage to the DAC, which the DIM14
// translates to lamp power. The DAC holds its last output across an MCU reset.
//

#include <stdint.h>
#include "config.h"
#include "SystemState.h"

class DFRobot_GP8403;  // fwd-decl; library type pulled in by the .cpp

class BulbController {
 public:
  // Initialise I2C and the three DACs. Returns false (and raises Fault::BulbDac
  // via the caller) if any board fails to answer. All outputs start at 0V.
  bool begin();

  // Set the target for one channel (0..255). Honoured with soft-start + stagger.
  void setTarget(uint8_t ch, uint8_t value);

  // Convenience: set all six at once.
  void setTargets(const uint8_t values[NUM_CHANNELS]);

  // Pump the ramps and push voltages to the DACs. Call every loop.
  void update();

  // Immediately command every channel dark (used on shutdown/fault). Ramps off
  // quickly rather than stepping, to be kind to the supply on the way down.
  void allOff();

  uint8_t current(uint8_t ch) const { return ch < NUM_CHANNELS ? (uint8_t)current_[ch] : 0; }
  bool    ok() const { return ok_; }

 private:
  void writeChannel(uint8_t ch, uint16_t milliVolts);
  uint16_t levelToMilliVolts(float level0to255) const;

  DFRobot_GP8403* dac_[3] = {nullptr, nullptr, nullptr};
  bool     boardOk_[3] = {false, false, false};
  float    current_[NUM_CHANNELS]  = {0};   // ramped value, 0..255
  uint8_t  target_[NUM_CHANNELS]   = {0};
  uint32_t releaseAt_[NUM_CHANNELS] = {0};  // channel held until this time (stagger)
  uint32_t lastUpdateMs_ = 0;
  bool     ok_ = false;
};
