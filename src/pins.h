#pragma once
//
// pins.h — GPIO assignments for the ESP32-S3 DevKitC-1.
//
// Strapping pins (GPIO 0, 45, 46) are avoided for outputs that must be quiet at
// boot. GPIO 19/20 are the native-USB D-/D+. GPIO 26..37 are reserved for the
// module's SPI flash/PSRAM on the -1 N8R8 board. GPIO 43/44 are UART0 (USB-CDC
// console). Everything below sits clear of those.
//
// IMPORTANT hardware notes (mirror these on the build):
//   * Each STEP line wants a 10k pull-DOWN to GND to swallow boot glitches.
//   * Each EN line wants a 10k pull-UP to 3V3 so the TMC2209s stay DISABLED
//     until the firmware deliberately enables them (EN is active-LOW).
//   * Mode switch and Sequence button use the internal pull-ups (active-LOW).
//
// If you change the board variant, re-verify every pin against its datasheet.

#include <stdint.h>
#include "config.h"

// --- Stepper STEP outputs (one per channel) -------------------------------
static constexpr uint8_t PIN_STEP[NUM_CHANNELS] = { 4, 5, 6, 7, 15, 16 };

// --- Stepper DIR outputs --------------------------------------------------
static constexpr uint8_t PIN_DIR[NUM_CHANNELS]  = { 8, 9, 10, 11, 17, 18 };

// --- Stepper EN outputs (active-LOW; default DISABLED via external pull-up) -
static constexpr uint8_t PIN_EN[NUM_CHANNELS]   = { 1, 2, 3, 12, 13, 14 };

// --- I2C (shared by the three GP8403 DACs) --------------------------------
static constexpr uint8_t PIN_I2C_SDA = 21;
static constexpr uint8_t PIN_I2C_SCL = 47;

// --- WS2812 status LEDs ---------------------------------------------------
// GPIO 38 is this DevKitC-1 revision's on-board addressable RGB LED (older boards
// put it on GPIO 48 — check your board). PIN_STATUS_LED_EXT is the off-board pixel
// that mirrors it (panel-mount status indicator). Both must be >= 32 (the bit-bang
// driver uses the GPIO_OUT1 register bank).
static constexpr uint8_t PIN_STATUS_LED     = 38;
static constexpr uint8_t PIN_STATUS_LED_EXT = 40;

// --- Front-panel controls (INPUT_PULLUP, active-LOW) ----------------------
// Mode switch is on GPIO 48 (free on this DevKitC-1 revision) because GPIO 38 is
// taken by the on-board RGB LED above.
static constexpr uint8_t PIN_MODE_SWITCH = 48;   // open = Gallery, closed = Performance
static constexpr uint8_t PIN_SEQ_BUTTON  = 39;   // momentary, debounced
