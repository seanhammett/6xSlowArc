#pragma once
//
// InputManager — debounces the front-panel Mode switch and Sequence button.
//
// Mode switch: a maintained toggle. Open (pulled up) = Gallery, closed =
// Performance. Read level-wise.
// Sequence button: a momentary, debounced push. Exposes a one-shot "pressed"
// edge that the main loop consumes to start/stop sequence playback.
//

#include <stdint.h>
#include "ChannelModel.h"

class InputManager {
 public:
  void begin();
  void update();

  Mode mode() const { return mode_; }

  // True for exactly one update() after a fresh button press (falling edge).
  bool sequencePressed() const { return seqPressed_; }

 private:
  Mode mode_ = Mode::Gallery;
  bool seqPressed_ = false;
};
