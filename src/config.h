#pragma once
//
// config.h — compile-time tunables for the Slow Arc Controller.
//
// Everything an installer or maintainer is likely to tweak lives here so the
// behavioural constants are not scattered through the modules. Pin assignments
// live in pins.h; WiFi credentials live in wifi_config.h.
//

#include <stdint.h>

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------
static constexpr uint8_t NUM_CHANNELS = 6;   // 6 Slow Arcs (bulb-pair + motor)

// ---------------------------------------------------------------------------
// Bulb dimming (DAC -> DIM14)
// ---------------------------------------------------------------------------
// Three DFRobot DFR0971 (GP8403) 2-channel DACs. Channel 0 of board[n] is the
// even Slow Arc, channel 1 is the odd one:
//   DAC channel 0 -> Arc {0,2,4}, DAC channel 1 -> Arc {1,3,5}
// I2C addresses are set on each board's DIP switch (range 0x58..0x5F).
static constexpr uint8_t  DAC_ADDRESSES[3] = { 0x58, 0x59, 0x5A };
static constexpr uint16_t DAC_FULLSCALE_MV = 10000;  // 0..10V output range

// NOTE: no firmware gamma curve. The 0..255 set-point maps linearly to 0..10V;
// the DIM14 dimmer applies its own perceptual correction downstream, so applying
// gamma here as well would double up and crush the low end.

// Soft-start: time to ramp a channel across its FULL 0..255 range. A partial
// change ramps proportionally faster. Required at power-on and on large jumps.
static constexpr uint32_t BULB_RAMP_MS = 350;        // within §8 100..500 ms

// Stagger: offset the six channels' turn-on so twelve cold filaments do not
// inrush simultaneously on the single 1000W rail.
static constexpr uint32_t BULB_STAGGER_MS = 45;

// A target increase larger than this (in 0..255 units) is treated as a "large
// increase" and gets re-staggered against any other channels also ramping up.
static constexpr uint8_t  BULB_LARGE_STEP = 24;

// ---------------------------------------------------------------------------
// Motors (TMC2209 in STEP/DIR, STEP pulses from MCPWM)
// ---------------------------------------------------------------------------
// Speed set-point 1..255 maps linearly onto MIN..MAX step rate. 0 = stopped.
// These are step pulses/second at whatever microstep the TMC2209 MS pins select
// in hardware. "Slow arc" => keep MAX modest; tune on the bench.
static constexpr uint32_t MOTOR_MAX_SPEED_HZ = 4000;
static constexpr uint32_t MOTOR_MIN_SPEED_HZ = 60;     // floor for speed==1
static constexpr uint32_t MOTOR_ACCEL_HZ_S  = 1500;    // ramp rate, steps/s^2
static constexpr bool     MOTOR_DIR_FORWARD = true;    // global rotation sense

// ---------------------------------------------------------------------------
// Status LED (WS2812)
// ---------------------------------------------------------------------------
static constexpr uint8_t  LED_BRIGHTNESS = 60;         // 0..255 master level

// ---------------------------------------------------------------------------
// Persistence (NVS)
// ---------------------------------------------------------------------------
// Don't hammer flash on every slider drag — coalesce writes this long after the
// last change before committing to NVS.
static constexpr uint32_t NVS_SAVE_DEBOUNCE_MS = 1500;

// ---------------------------------------------------------------------------
// Networking — WiFi is OPTIONAL to operation. Never block the control loop.
// ---------------------------------------------------------------------------
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;  // give up, run offline
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 30000;  // bounded reconnect
static constexpr bool     WIFI_AP_FALLBACK = true;          // SoftAP for commissioning
static constexpr uint32_t WIFI_AP_LINGER_MS = 60000;        // keep AP up this long
                                                            // after a portal join
static constexpr char     WIFI_AP_SSID[]   = "SlowArc-Setup";
static constexpr char     WIFI_AP_PASS[]   = "slowarc123";   // >= 8 chars
static constexpr char     OTA_HOSTNAME[]   = "slow-arc";

// ---------------------------------------------------------------------------
// Sequence engine (Performance mode)
// ---------------------------------------------------------------------------
// What "Stop" does. true  = hold the values the sequence last produced.
//                   false = snap back to the static gallery set-points.
static constexpr bool SEQUENCE_STOP_HOLD = true;

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
static constexpr uint32_t WDT_TIMEOUT_S = 8;
