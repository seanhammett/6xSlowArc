#pragma once
//
// StatusLed — the single WS2812, the box's only on-board diagnostic.
//
// Maps a high-level LedState to colour/animation per spec §7. A fault is sticky
// in presentation: the red blink count encodes the subsystem (2 = bulb/DAC,
// 3 = motor, 4 = supply/brownout). All animation is non-blocking — update() is
// driven from the main loop.
//
//   State                         Indication
//   ----------------------------  --------------------------
//   Boot / soft-start             solid amber
//   Gallery, healthy              slow green pulse
//   Performance, running          slow blue pulse
//   WiFi connecting               amber pulse
//   OTA in progress               fast blue pulse
//   Fault                         red blink, count = subsystem
//

#include <stdint.h>
#include "SystemState.h"

class StatusLed {
 public:
  void begin();

  // Set the presentation state. For LedState::Fault, pass the subsystem code.
  void set(LedState state, Fault fault = Fault::None);

  // Render the current frame. Call every loop.
  void update();

  // Introspection (diagnostics / web mirror): the current state, and the exact
  // colour being shown as 0xRRGGBB (after brightness + animation).
  LedState state() const { return state_; }
  uint32_t rgb() const {
    return ((uint32_t)lastR_ << 16) | ((uint32_t)lastG_ << 8) | lastB_;
  }

 private:
  LedState state_ = LedState::Boot;
  Fault    fault_ = Fault::None;
  uint8_t  lastR_ = 0, lastG_ = 0, lastB_ = 0;   // last colour shown
};
