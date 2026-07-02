#include "InputManager.h"

#include <Arduino.h>
#include <Bounce2.h>

#include "pins.h"

namespace {
Bounce modeSwitch = Bounce();
Bounce seqButton  = Bounce();
constexpr uint16_t DEBOUNCE_MS = 25;
}

void InputManager::begin() {
  modeSwitch.attach(PIN_MODE_SWITCH, INPUT_PULLUP);
  modeSwitch.interval(DEBOUNCE_MS);
  seqButton.attach(PIN_SEQ_BUTTON, INPUT_PULLUP);
  seqButton.interval(DEBOUNCE_MS);

  modeSwitch.update();
  // Closed (LOW) = Performance, open (HIGH) = Gallery.
  mode_ = modeSwitch.read() == LOW ? Mode::Performance : Mode::Gallery;
}

void InputManager::update() {
  modeSwitch.update();
  seqButton.update();

  mode_ = modeSwitch.read() == LOW ? Mode::Performance : Mode::Gallery;

  // Active-LOW momentary button -> falling edge is a press.
  seqPressed_ = seqButton.fell();
}
