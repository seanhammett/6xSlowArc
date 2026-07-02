//
// test_stepper.cpp — standalone bench test for ONE stepper channel (TMC2209 in
// STEP/DIR, driven by FastAccelStepper).
//
// Built only by the `test_stepper` PlatformIO environment (see platformio.ini);
// the main firmware excludes it. Flash it, wire up one TMC2209 + motor, and watch
// the serial console — it spins the motor forward, then reverse, then does a
// fixed relative move, printing position/speed so you can confirm it steps.
//
//   pio run -e test_stepper -t upload && pio device monitor -e test_stepper
//
// Wiring for channel N comes from pins.h (STEP[N]/DIR[N]/EN[N]); EN is active-LOW
// and wants its 10k pull-UP to 3V3 so the driver stays disabled until we enable
// it. Pick the channel under test with TEST_STEP_CH (0..5).
//

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "pins.h"     // PIN_STEP / PIN_DIR / PIN_EN — same wiring as the real rig
#include "config.h"   // MOTOR_* tunables

// Which Arc channel's stepper to test. Override from the env, e.g. -DTEST_STEP_CH=3.
#ifndef TEST_STEP_CH
#define TEST_STEP_CH 0
#endif
static_assert(TEST_STEP_CH < NUM_CHANNELS, "TEST_STEP_CH must be 0..NUM_CHANNELS-1");

// Test motion parameters (override from the env if you like).
#ifndef TEST_SPEED_HZ
#define TEST_SPEED_HZ 800       // step pulses/sec while spinning continuously
#endif
#ifndef TEST_MOVE_STEPS
#define TEST_MOVE_STEPS 3200    // relative move length for the position test
#endif

static const uint32_t kRunMs   = 4000;   // how long to spin each direction
static const uint32_t kPauseMs = 1500;   // dwell, motor idle (enabled, no pulses)

static FastAccelStepperEngine engine = FastAccelStepperEngine();
static FastAccelStepper*       stepper = nullptr;

// --- Helpers --------------------------------------------------------------

static void printState(const char* tag) {
  Serial.printf("[stp] %-14s pos=%ld  speed=%ld Hz  running=%d\n",
                tag,
                (long)stepper->getCurrentPosition(),
                (long)(stepper->getCurrentSpeedInMilliHz() / 1000),
                stepper->isRunning() ? 1 : 0);
}

// Spin continuously for `ms`, printing state about twice a second.
static void spinFor(uint32_t ms, const char* tag) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    printState(tag);
    delay(500);
  }
}

// --- Lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== single stepper bench test ===");
  Serial.printf("Channel        : %u\n", (unsigned)TEST_STEP_CH);
  Serial.printf("Pins           : STEP=%u  DIR=%u  EN=%u (active-LOW)\n",
                PIN_STEP[TEST_STEP_CH], PIN_DIR[TEST_STEP_CH], PIN_EN[TEST_STEP_CH]);
  Serial.printf("Speed          : %u Hz   accel=%lu Hz/s\n",
                (unsigned)TEST_SPEED_HZ, (unsigned long)MOTOR_ACCEL_HZ_S);

  // Belt-and-braces: hold EN disabled (active-LOW => HIGH) before the engine
  // takes the pin, so there's no window where the driver is live.
  pinMode(PIN_EN[TEST_STEP_CH], OUTPUT);
  digitalWrite(PIN_EN[TEST_STEP_CH], HIGH);

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP[TEST_STEP_CH]);
  if (!stepper) {
    Serial.println("[stp] FAILED to connect STEP pin (no free RMT channel?). Halting.");
    return;                         // loop() will report and idle
  }

  stepper->setDirectionPin(PIN_DIR[TEST_STEP_CH], /*dirHighCountsUp=*/MOTOR_DIR_FORWARD);
  stepper->setEnablePin(PIN_EN[TEST_STEP_CH], /*low_active_enables_stepper=*/true);
  stepper->setAutoEnable(false);            // we own EN explicitly
  stepper->setAcceleration(MOTOR_ACCEL_HZ_S);
  stepper->setSpeedInHz(TEST_SPEED_HZ);
  stepper->enableOutputs();                 // EN -> LOW, driver live (idle, no pulses)

  Serial.println("[stp] OK — driver enabled. Starting motion sequence.\n");
}

void loop() {
  if (!stepper) {
    Serial.println("[stp] no stepper — check wiring/channel, then reset.");
    delay(2000);
    return;
  }

  // 1) Spin forward.
  Serial.printf("[stp] >>> FORWARD @ %u Hz for %lu ms\n",
                (unsigned)TEST_SPEED_HZ, (unsigned long)kRunMs);
  stepper->setSpeedInHz(TEST_SPEED_HZ);
  stepper->runForward();
  spinFor(kRunMs, "forward");

  // 2) Decelerate to a stop and dwell.
  Serial.println("[stp] --- STOP (decelerate)");
  stepper->stopMove();
  while (stepper->isRunning()) delay(10);
  printState("stopped");
  delay(kPauseMs);

  // 3) Spin reverse.
  Serial.printf("[stp] <<< REVERSE @ %u Hz for %lu ms\n",
                (unsigned)TEST_SPEED_HZ, (unsigned long)kRunMs);
  stepper->runBackward();
  spinFor(kRunMs, "reverse");

  Serial.println("[stp] --- STOP (decelerate)");
  stepper->stopMove();
  while (stepper->isRunning()) delay(10);
  printState("stopped");
  delay(kPauseMs);

  // 4) Fixed relative move out and back, to verify exact step counts.
  Serial.printf("[stp] === MOVE +%d steps\n", (int)TEST_MOVE_STEPS);
  stepper->move(TEST_MOVE_STEPS);
  while (stepper->isRunning()) { printState("moving +"); delay(250); }
  printState("moved +");
  delay(kPauseMs);

  Serial.printf("[stp] === MOVE -%d steps (back to start)\n", (int)TEST_MOVE_STEPS);
  stepper->move(-TEST_MOVE_STEPS);
  while (stepper->isRunning()) { printState("moving -"); delay(250); }
  printState("moved -");
  delay(kPauseMs);

  Serial.println("[stp] --- cycle complete, repeating ---\n");
}
