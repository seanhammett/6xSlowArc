#pragma once
//
// SequenceEngine — Performance-mode choreography (spec §5).
//
// A small timeline interpreter. The choreography is a list of Cues; each Cue is
// a target frame for all six channels to be reached at a given timestamp.
// Between cues the engine linearly interpolates, so the artwork eases between
// states rather than snapping. The timeline loops.
//
// Cue values are SCALES OF THE STORED SET-POINTS, not absolute levels:
// 255 = the channel's commissioned set-point, 0 = off. The choreography thus
// only decides WHEN each arc moves; HOW BRIGHT/FAST it goes stays with the
// per-channel set-points, so re-commissioning retunes the piece automatically.
//
// The choreography table lives in SequenceEngine.cpp (kSequenceCues). To change
// the piece, edit that table (or call load()); nothing else changes. This is
// deliberately the only thing that needs editing.
//
// Output: while running, fill() scales the set-points by the interpolated frame
// into the caller's effective brightness/speed arrays, which then feed the
// controllers. In Gallery mode the engine is dormant and contributes nothing.
//
// Stop behaviour is set by SEQUENCE_STOP_HOLD (config.h): hold the last produced
// frame (default) or hand back to the static gallery set-points.
//

#include <stdint.h>
#include "config.h"

struct Cue {
  uint32_t timeMs;                      // offset from sequence start
  uint8_t  brightness[NUM_CHANNELS];    // scale of the bulb set-point (255 = set-point)
  uint8_t  speed[NUM_CHANNELS];         // scale of the motor set-point (255 = set-point)
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

  // While running, overwrite outBrightness/outSpeed with the current frame:
  // the given set-points scaled by the interpolated cue values. No-op when
  // stopped. Returns true if it wrote a frame.
  bool fill(const uint8_t setBrightness[NUM_CHANNELS],
            const uint8_t setSpeed[NUM_CHANNELS],
            uint8_t outBrightness[NUM_CHANNELS],
            uint8_t outSpeed[NUM_CHANNELS]);

  // Introspection for the web UI's timeline view.
  const Cue* cues() const { return cues_; }
  uint16_t   cueCount() const { return count_; }
  uint32_t   loopLengthMs() const { return count_ ? cues_[count_ - 1].timeMs : 0; }
  uint32_t   positionMs() const;        // ms into the loop; 0 when stopped

 private:
  const Cue* cues_  = nullptr;
  uint16_t   count_ = 0;
  bool       running_ = false;
  uint32_t   startMs_ = 0;
};
