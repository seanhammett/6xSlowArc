#pragma once
//
// SequenceEngine — Performance-mode choreography (spec §5).
//
// A small timeline interpreter. The choreography is a SeqDef — an artist-edited
// list of steps ("at time T these arcs are on", bulb + motor together) with one
// shared ramp — created on the web UI's Sequences tab and persisted by
// SequenceStore. The engine eases between states rather than snapping, and the
// timeline loops. The step semantics live in SeqFrame.h, which evaluates the
// frame straight from the steps (no expanded cue table: at 1000 steps that
// table would cost ~32 KB of RAM).
//
// Frame values are SCALES OF THE STORED SET-POINTS, not absolute levels:
// 255 = the channel's commissioned set-point, 0 = off. The choreography thus
// only decides WHEN each arc moves; HOW BRIGHT/FAST it goes stays with the
// per-channel set-points, so re-commissioning retunes the piece automatically.
// (A scale applies to the motor's Hz set-point the same way — half scale is
// half the commissioned step rate.)
//
// Output: while running, fill() scales the set-points by the current frame into
// the caller's effective brightness/speed arrays, which then feed the
// controllers. In Gallery mode the engine is dormant and contributes nothing.
//
// Threading: everything is driven from the loop task, but the web task reads the
// status accessors (running, position, names, loop length, run id) for its JSON
// snapshot and serialises the whole piece through snapshot(), which copies under
// a critical section so it never sees an apply() half-done.
//
// Stop behaviour is set by SEQUENCE_STOP_HOLD (config.h): hold the last produced
// frame (default) or hand back to the static gallery set-points.
//

#include <stdint.h>
#include "config.h"
#include "SeqFrame.h"

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
  // the given set-points scaled by the frame's per-channel scale. No-op when
  // stopped. Returns true if it wrote a frame.
  bool fill(const uint8_t  setBrightness[NUM_CHANNELS],
            const uint16_t setSpeedHz[NUM_CHANNELS],
            uint8_t  outBrightness[NUM_CHANNELS],
            uint16_t outSpeedHz[NUM_CHANNELS]);

  // Shift the running piece's clock by ms (+ = forward, - = back, never before
  // this run's start). How a browser playing the audio keeps the lights in step
  // with its track without ever touching the audio. No-op when stopped.
  void nudge(int32_t ms);

  uint32_t loopLengthMs() const { return loopLen_; }
  uint32_t positionMs() const;          // ms into the loop; 0 when stopped

  // Increments on every start(). Lets the web page's audio tell a restart from
  // ordinary progress even when a stop + play both land between two polls.
  uint32_t runId() const { return runId_; }

  // Copy the piece the engine is playing (not a queued one) for the web UI's
  // timeline. Safe from any task. `out` is ~8 KB: pass a heap buffer.
  void snapshot(SeqDef& out) const;

  // Which piece the engine actually IS, and which one is waiting to replace it
  // at the next start(). The web UI needs both: selecting a sequence mid-play
  // deliberately changes nothing until then, and without saying so the
  // unchanged timeline looks like the selection was lost.
  const char* name() const { return def_.name; }
  const char* queuedName() const { return hasQueued_ ? queued_.name : ""; }

 private:
  void setDef(const SeqDef& def);      // copy in under the lock, refresh loopLen_

  SeqDef   def_       = {};            // the piece being played
  SeqDef   queued_    = {};            // applied at next start() when running
  bool     hasQueued_ = false;
  bool     running_   = false;
  uint32_t startMs_   = 0;
  uint32_t loopLen_   = 0;             // cached: read from the web task
  uint32_t runId_     = 0;
};
