#include "StatusLed.h"

#include <Arduino.h>
#include <math.h>

#include "soc/gpio_reg.h"
#include "esp_cpu.h"

#include "pins.h"

// Status is shown on two WS2812 pixels driven identically: the on-board one
// (PIN_STATUS_LED) and an optional off-board mirror (PIN_STATUS_LED_EXT). We
// bit-bang rather than use a library/RMT: both pins live in the GPIO_OUT1 register
// bank, so one combined mask drives both data lines in lockstep from a single
// waveform — the two pixels always show the same colour. (Driving an unpopulated
// off-board pin is harmless.)
//
// A cycle-counted bit-bang is trivial and reliable: 24 bits of ~1.25 us each
// => ~30 us with interrupts masked, well within the loop's budget and far below
// the WS2812's >50 us reset gap.

namespace {
// WS2812 bit timing in CPU cycles at 240 MHz (1 us = 240 cycles).
//   '1' : ~0.8 us high, ~0.45 us low      '0' : ~0.4 us high, ~0.85 us low
constexpr uint32_t T1H = 192, T1L = 108;
constexpr uint32_t T0H = 96,  T0L = 204;

static_assert(PIN_STATUS_LED >= 32 && PIN_STATUS_LED_EXT >= 32,
              "bit-bang uses the GPIO_OUT1 (pins >=32) registers");
// Both data lines set/cleared together -> both pixels get the same waveform.
constexpr uint32_t LED_MASK = (1u << (PIN_STATUS_LED - 32)) |
                              (1u << (PIN_STATUS_LED_EXT - 32));

// Send three colour bytes (already in WS2812 G,R,B order) out both data lines.
void IRAM_ATTR sendPixel(uint8_t g, uint8_t r, uint8_t b) {
  const uint8_t bytes[3] = { g, r, b };
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  taskENTER_CRITICAL(&mux);
  for (uint8_t i = 0; i < 3; ++i) {
    uint8_t v = bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t hi = (v & 0x80) ? T1H : T0H;
      const uint32_t lo = (v & 0x80) ? T1L : T0L;
      v <<= 1;
      const uint32_t start = esp_cpu_get_cycle_count();
      REG_WRITE(GPIO_OUT1_W1TS_REG, LED_MASK);                     // drive high
      while (esp_cpu_get_cycle_count() - start < hi) {}
      REG_WRITE(GPIO_OUT1_W1TC_REG, LED_MASK);                     // drive low
      while (esp_cpu_get_cycle_count() - start < hi + lo) {}
    }
  }
  taskEXIT_CRITICAL(&mux);
}

// A triangle 0..1..0 pulse over `periodMs`.
float pulse(uint32_t periodMs) {
  float phase = (float)(millis() % periodMs) / (float)periodMs;  // 0..1
  return 1.0f - fabsf(2.0f * phase - 1.0f);                      // 0..1..0
}

// Scale an 8-bit channel by 0..1 and by the master LED brightness.
uint8_t scale(uint8_t c, float k) {
  return (uint8_t)lroundf(c * k * (LED_BRIGHTNESS / 255.0f));
}
}  // namespace

void StatusLed::begin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_STATUS_LED_EXT, OUTPUT);
  digitalWrite(PIN_STATUS_LED_EXT, LOW);
  sendPixel(0, 0, 0);                 // clear both
}

void StatusLed::set(LedState state, Fault fault) {
  state_ = state;
  fault_ = fault;
}

void StatusLed::update() {
  // Each state yields a base colour (full-scale r,g,b) and an intensity k; the
  // master brightness and k are applied exactly once, below.
  uint8_t r = 0, g = 0, b = 0;
  float   k = 1.0f;

  switch (state_) {
    case LedState::Boot:
      r = 255; g = 90; b = 0;                       // solid amber
      break;

    case LedState::GalleryHealthy:
      g = 255;
      k = 0.15f + 0.85f * pulse(2600);              // slow green pulse
      break;

    case LedState::PerformanceRun:
      b = 255;
      k = 0.15f + 0.85f * pulse(2600);              // slow blue pulse
      break;

    case LedState::WifiConnecting:
      r = 255; g = 90;
      k = 0.10f + 0.90f * pulse(900);               // amber pulse
      break;

    case LedState::Ota:
      b = 255;
      k = 0.10f + 0.90f * pulse(280);               // fast blue pulse
      break;

    case LedState::Fault: {
      // Blink the subsystem code: N quick blinks, then a pause, repeat.
      uint8_t count = (uint8_t)fault_;
      if (count == 0) count = 1;
      const uint32_t blinkMs = 250;
      const uint32_t gapMs   = 1200;
      uint32_t cycle = count * 2 * blinkMs + gapMs;
      uint32_t t = millis() % cycle;
      bool on = (t < (uint32_t)count * 2 * blinkMs) && (((t / blinkMs) & 1) == 0);
      r = 255;
      k = on ? 1.0f : 0.0f;
      break;
    }
  }

  // WS2812 wants G,R,B order. Keep the scaled values for the rgb() mirror.
  lastR_ = scale(r, k);
  lastG_ = scale(g, k);
  lastB_ = scale(b, k);
  sendPixel(lastG_, lastR_, lastB_);
}
