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
// (A scale applies to the motor's Hz set-point the same way — half scale is
// half the commissioned step rate.)
//
// Choreographies arrive as a SeqDef — an artist-editable list of steps ("at
// time T these arcs are on", bulb + motor together) with one shared ramp —
// created on the web UI's Sequences tab and persisted by SequenceStore.
// apply() expands the steps into the internal cue table: each step becomes a
// hold-until (T - ramp) cue plus a target cue at T, and the loop closes at
// lastT + ramp back to the t=0 state.
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

// One step of an artist-editable sequence: the state to be fully reached at
// timeMs. Bulb and motor always move together, so an arc is a single bit.
struct SeqStep {
  uint32_t timeMs;                      // when the new state is fully reached
  uint8_t  mask;                        // bit i = arc i on
};

struct SeqDef {
  char     name[24];                    // NUL-terminated display name
  uint32_t rampMs;                      // shared rise/fall time for every change
  uint8_t  stepCount;
  SeqStep  steps[SEQ_MAX_STEPS];        // sorted by timeMs
};

class SequenceEngine {
 public:
  void begin();

  // Make def the choreography: immediately when stopped; while running it is
  // queued and takes effect at the next start() ("plays on the next push").
  void apply(const SeqDef& def);

  void start();                         // (re)start playback from t=0
  void stop();                          // stop; behaviour per SEQUENCE_STOP_HOLD
  void toggle() { running_ ? stop() : start(); }

  bool running() const { return running_; }

  // While running, overwrite outBrightness/outSpeed with the current frame:
  // the given set-points scaled by the interpolated cue values. No-op when
  // stopped. Returns true if it wrote a frame.
  bool fill(const uint8_t  setBrightness[NUM_CHANNELS],
            const uint16_t setSpeedHz[NUM_CHANNELS],
            uint8_t  outBrightness[NUM_CHANNELS],
            uint16_t outSpeedHz[NUM_CHANNELS]);

  // Introspection for the web UI's timeline view.
  const Cue* cues() const { return cues_; }
  uint16_t   cueCount() const { return count_; }
  uint32_t   loopLengthMs() const { return count_ ? cues_[count_ - 1].timeMs : 0; }
  uint32_t   positionMs() const;        // ms into the loop; 0 when stopped

  // Which piece the cue table above actually IS, and which one is waiting to
  // replace it at the next start(). The web UI needs both: selecting a sequence
  // mid-play deliberately changes nothing until then, and without saying so the
  // unchanged timeline looks like the selection was lost.
  const char* name() const { return name_; }
  const char* queuedName() const { return hasQueued_ ? queued_.name : ""; }

 private:
  void expand(const SeqDef& def);      // steps + ramp -> cue table in buf_

  // Expanded cue table: per step a hold cue + a target cue, plus the t=0 and
  // loop-closing entries.
  Cue        buf_[2 * SEQ_MAX_STEPS + 2];
  const Cue* cues_  = nullptr;
  uint16_t   count_ = 0;
  char       name_[sizeof(SeqDef::name)] = "";   // the expanded piece's name
  bool       running_ = false;
  uint32_t   startMs_ = 0;
  SeqDef     queued_;                  // applied at next start() when running
  bool       hasQueued_ = false;
};
