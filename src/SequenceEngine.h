#pragma once
//
// SequenceEngine — Performance-mode choreography (spec §5).
//
// A small timeline interpreter. The choreography is a list of Cues; each Cue is
// a target frame (brightness + speed for all six channels) to be reached at a
// given timestamp. Between cues the engine linearly interpolates, so the artwork
// eases between states rather than snapping. The timeline loops.
//
// The real choreography is TBD — kPlaceholderCues below is a gentle demo. To add
// the artist's piece, replace that table (or call load()) with new cues; nothing
// else changes. This is deliberately the only thing that needs editing.
//
// Output: while running, fill() writes the interpolated frame into the caller's
// effective brightness/speed arrays, which then feed the controllers. In Gallery
// mode the engine is dormant and contributes nothing.
//
// Stop behaviour is set by SEQUENCE_STOP_HOLD (config.h): hold the last produced
// frame (default) or hand back to the static gallery set-points.
//

#include <stdint.h>
#include "config.h"

struct Cue {
  uint32_t timeMs;                      // offset from sequence start
  uint8_t  brightness[NUM_CHANNELS];
  uint8_t  speed[NUM_CHANNELS];
};

class SequenceEngine {
 public:
  void begin();

  // Provide a custom timeline (must be sorted by timeMs, count >= 1). If never
  // called, the built-in placeholder is used.
  void load(const Cue* cues, uint16_t count);

  void start();                         // (re)start playback from t=0
  void stop();                          // stop; behaviour per SEQUENCE_STOP_HOLD
  void toggle() { running_ ? stop() : start(); }

  bool running() const { return running_; }

  // While running, overwrite outBrightness/outSpeed with the current frame.
  // No-op when stopped. Returns true if it wrote a frame.
  bool fill(uint8_t outBrightness[NUM_CHANNELS], uint8_t outSpeed[NUM_CHANNELS]);

 private:
  const Cue* cues_  = nullptr;
  uint16_t   count_ = 0;
  bool       running_ = false;
  uint32_t   startMs_ = 0;
};
