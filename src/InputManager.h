#pragma once
//
// InputManager — debounces the front-panel Mode switch and Sequence button.
//
// Mode switch: a maintained toggle. Open (pulled up) = Gallery, closed =
// Performance. It is one of two sources of mode changes — the web page is the
// other, and the last change wins — so main acts on the flip (modeChanged()),
// not the level. mode() is the switch's position, which may not be the box's
// mode once the web has overridden it.
// Sequence button: a momentary, debounced push. Exposes a one-shot "pressed"
// edge that the main loop consumes to start/stop sequence playback.
//

#include <stdint.h>
#include "ChannelModel.h"

class InputManager {
 public:
  void begin();
  void update();

  Mode mode() const { return mode_; }         // switch position

  // True for exactly one update() after the switch is flipped.
  bool modeChanged() const { return modeChanged_; }

  // True for exactly one update() after a fresh button press (falling edge).
  bool sequencePressed() const { return seqPressed_; }

  // millis() of the last button press, 0 if none since boot. For the status
  // page, which polls too slowly to catch the press itself.
  uint32_t lastPressMs() const { return lastPressMs_; }

 private:
  Mode mode_ = Mode::Gallery;
  bool modeChanged_ = false;
  bool seqPressed_ = false;
  uint32_t lastPressMs_ = 0;
};
