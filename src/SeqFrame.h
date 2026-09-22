#pragma once
//
// SeqFrame — the sequence data types and the pure "what is the frame at time t"
// evaluator. No Arduino dependency, so the native unit test (test/test_frame)
// compiles it on the host.
//
// A sequence is a sorted list of steps ("at time T these arcs are on") plus one
// shared ramp. Each step's state is reached AT its time, ramping linearly from
// the previous state over the preceding `ramp` ms (squeezed when steps are
// closer together than the ramp). Before the first step the piece holds the LAST
// step's state, so the loop is seamless, and the loop closes at lastT + ramp.
//
// This evaluates exactly what the old expanded cue table did — per step a
// hold-until (T - ramp) cue plus a target cue at T, with same-time duplicates
// collapsed so the later step wins — without materialising the table, which at
// SEQ_MAX_STEPS = 1000 would cost ~32 KB of RAM.
//

#include <stdint.h>
#include "config.h"

// One step: the state to be fully reached at timeMs. Bulb and motor always move
// together, so an arc is a single bit.
struct SeqStep {
  uint32_t timeMs;                      // when the new state is fully reached
  uint8_t  mask;                        // bit i = arc i on
};

struct SeqDef {
  char     name[24];                    // NUL-terminated display name
  uint32_t rampMs;                      // shared rise/fall time for every change
  uint16_t stepCount;
  SeqStep  steps[SEQ_MAX_STEPS];        // sorted by timeMs
};

// Loop length: the last step plus the closing ramp. 0 for an empty sequence.
inline uint32_t seqLoopLengthMs(const SeqDef& d) {
  return d.stepCount ? d.steps[d.stepCount - 1].timeMs + d.rampMs : 0;
}

namespace seqframe {
// a + (b - a) * num / den, truncating toward zero — the old engine's lerp8, kept
// bit-for-bit so the rewrite changes no output.
inline uint8_t lerp8(uint8_t a, uint8_t b, uint32_t num, uint32_t den) {
  if (den == 0) return a;
  int32_t d = (int32_t)b - (int32_t)a;
  return (uint8_t)((int32_t)a + d * (int32_t)num / (int32_t)den);
}

// First index in [lo, count) whose timeMs > t (count if none).
inline uint16_t upperBound(const SeqDef& d, uint32_t t, uint16_t lo = 0) {
  uint16_t hi = d.stepCount;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    if (d.steps[mid].timeMs <= t) lo = mid + 1;
    else                          hi = mid;
  }
  return lo;
}
}  // namespace seqframe

// Per-channel scale (0..255, 255 = the commissioned set-point) at loop time t,
// 0 <= t < seqLoopLengthMs(d). Returns false for an empty sequence.
inline bool seqFrameAt(const SeqDef& d, uint32_t t, uint8_t scale[NUM_CHANNELS]) {
  using namespace seqframe;
  if (d.stepCount == 0) return false;

  const uint16_t n    = d.stepCount;
  const uint8_t  last = d.steps[n - 1].mask;
  const uint16_t k    = upperBound(d, t);          // next step still ahead of t

  uint8_t from, to;
  uint32_t num = 0, den = 0;
  if (k == n) {
    from = to = last;                              // closing ramp: holds the last state
  } else {
    // Same-time duplicates collapse to the LAST of the group (the old table
    // overwrote same-time cues), so the target is the group's final step.
    const uint32_t tk   = d.steps[k].timeMs;
    const uint16_t j    = upperBound(d, tk, k) - 1;
    const uint32_t prevT = k ? d.steps[k - 1].timeMs : 0;
    uint32_t hold = (tk > d.rampMs) ? tk - d.rampMs : 0;
    if (hold < prevT) hold = prevT;                // ramp squeezed by close steps
    from = k ? d.steps[k - 1].mask : last;         // before step 0: loop-seamless
    to   = d.steps[j].mask;
    if (t < hold) {
      to = from;                                   // holding before the ramp
    } else {
      num = t - hold;
      den = tk - hold;
    }
  }

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    uint8_t a = (from >> ch) & 1 ? 255 : 0;
    uint8_t b = (to   >> ch) & 1 ? 255 : 0;
    scale[ch] = lerp8(a, b, num, den);
  }
  return true;
}
