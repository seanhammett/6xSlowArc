#pragma once
//
// SystemState — small shared types describing overall health and the value the
// status LED should render. Kept dependency-free so any module can raise a
// fault without pulling in the LED driver.
//

#include <stdint.h>

// Fault subsystem codes double as the WS2812 red-blink count (spec §7).
enum class Fault : uint8_t {
  None    = 0,
  BulbDac = 2,   // DAC init / I2C failure
  Motor   = 3,   // stepper engine failure
  Supply  = 4,   // brownout / unexpected reset attributed to the rail
};

// High-level presentation states for the status LED (spec §7).
enum class LedState : uint8_t {
  Boot,             // solid amber  — boot / soft-start
  GalleryHealthy,   // slow green pulse
  PerformanceRun,   // slow blue pulse
  WifiConnecting,   // amber pulse
  Ota,              // fast blue pulse
  Fault,            // red blink, count encodes subsystem
};
