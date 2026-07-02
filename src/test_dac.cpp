//
// test_dac.cpp — standalone bench test for ONE DFRobot GP8403 (DFR0971) DAC.
//
// Built only by the `test_dac` PlatformIO environment (see platformio.ini); the
// main firmware excludes it. Flash it, attach a multimeter to the board's VOUT0
// / VOUT1 screw terminals (referenced to the board GND), and watch the serial
// console — each step prints the voltage it just commanded so you can confirm
// the measured output matches.
//
//   pio run -e test_dac -t upload && pio device monitor -e test_dac
//
// Wiring (matches the real rig, pins.h): SDA=GPIO21, SCL=GPIO47, plus 3V3 + GND
// to the DAC board. Set the board's address DIP switch and TEST_DAC_ADDR to match
// (the three production boards are 0x58 / 0x59 / 0x5A).
//

#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_GP8403.h>

#include "pins.h"     // PIN_I2C_SDA / PIN_I2C_SCL — same wiring as the real rig
#include "config.h"   // DAC_FULLSCALE_MV

// Which board to test. Override from the env with -DTEST_DAC_ADDR=0x59 if needed.
#ifndef TEST_DAC_ADDR
#define TEST_DAC_ADDR 0x58
#endif

static DFRobot_GP8403 dac(&Wire, TEST_DAC_ADDR);
static bool dacOk = false;

// Voltages to step through, in millivolts (within the 0..10000 mV / 10 V range).
static const uint16_t kStepsMv[] = { 0, 2500, 5000, 7500, 10000, 5000, 0 };
static const size_t   kNumSteps  = sizeof(kStepsMv) / sizeof(kStepsMv[0]);
static const uint32_t kHoldMs    = 2500;   // dwell per step, time to read the meter

// --- Helpers --------------------------------------------------------------

static void i2cScan() {
  Serial.println("[i2c] scanning bus...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c]   device found at 0x%02X%s\n", addr,
                    addr == TEST_DAC_ADDR ? "  <- target DAC" : "");
      ++found;
    }
  }
  if (found == 0) {
    Serial.println("[i2c]   NONE found — check 3V3/GND, SDA/SCL wiring, and pull-ups.");
  }
  Serial.printf("[i2c] scan done, %u device(s).\n\n", found);
}

// --- Lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(400);                       // let the console attach
  Serial.println("\n=== GP8403 single-DAC bench test ===");
  Serial.printf("Target address : 0x%02X\n", TEST_DAC_ADDR);
  Serial.printf("I2C pins       : SDA=%u  SCL=%u\n", PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.printf("Full-scale     : %u mV (10 V range)\n\n", (unsigned)DAC_FULLSCALE_MV);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  i2cScan();

  Serial.println("[dac] begin()...");
  if (dac.begin() != 0) {
    Serial.println("[dac] FAILED — board did not ACK. Fix wiring/address, then reset.");
    Serial.println("[dac] (will keep retrying every 2 s)");
    return;                         // loop() will retry
  }
  dac.setDACOutRange(DFRobot_GP8403::eOutputRange10V);
  dacOk = true;
  Serial.println("[dac] OK — 10 V range selected. Starting output sweep.\n");
}

void loop() {
  // Not initialised yet: keep retrying so the board can be plugged in live.
  if (!dacOk) {
    delay(2000);
    if (dac.begin() == 0) {
      dac.setDACOutRange(DFRobot_GP8403::eOutputRange10V);
      dacOk = true;
      Serial.println("[dac] OK now — starting output sweep.\n");
    } else {
      Serial.println("[dac] still not responding...");
    }
    return;
  }

  // Step both channels together (channel 2 = both) through the voltage table.
  for (size_t i = 0; i < kNumSteps; ++i) {
    uint16_t mv = kStepsMv[i];
    dac.setDACOutVoltage(mv, /*channel=*/2);
    Serial.printf("[dac] VOUT0 & VOUT1 = %5u mV  (%.2f V) — measure now\n",
                  mv, mv / 1000.0f);
    delay(kHoldMs);
  }

  // Then exercise the two channels independently so a stuck/swapped channel is
  // obvious: ch0 high while ch1 low, then swap.
  Serial.println("[dac] channel check: VOUT0=10.00 V, VOUT1=0.00 V");
  dac.setDACOutVoltage(DAC_FULLSCALE_MV, 0);
  dac.setDACOutVoltage(0, 1);
  delay(kHoldMs);

  Serial.println("[dac] channel check: VOUT0=0.00 V, VOUT1=10.00 V\n");
  dac.setDACOutVoltage(0, 0);
  dac.setDACOutVoltage(DAC_FULLSCALE_MV, 1);
  delay(kHoldMs);
}
