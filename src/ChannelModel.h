#pragma once
//
// ChannelModel — the single source of the persisted control values.
//
// Holds, for all six channels, the brightness and speed SET-POINTS (0..255)
// plus the current operating mode. These originate from the web UI or, on first
// boot, the compiled-in defaults, and are persisted to NVS by ConfigStore.
//
// The per-channel "current" (ramping) values referenced in the spec live inside
// BulbController and MotorController, which own the ramps — keeping them there
// avoids a second copy that could drift from the hardware state. ChannelModel is
// purely the target/config layer.
//

#include <stdint.h>
#include "config.h"

enum class Mode : uint8_t {
  Gallery     = 0,   // set-and-hold (default)
  Performance = 1,   // sequenced choreography
};

struct ChannelModel {
  uint8_t brightness[NUM_CHANNELS];   // 0..255 set-point
  uint8_t speed[NUM_CHANNELS];        // 0..255 set-point (0 = motor stopped)
  Mode    mode;

  // Compiled-in first-boot defaults (used only when NVS is empty). Conservative:
  // dim and slow so a fresh, un-commissioned box can't surprise anyone.
  void loadDefaults() {
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      brightness[i] = 40;
      speed[i]      = 20;
    }
    mode = Mode::Gallery;
  }
};
