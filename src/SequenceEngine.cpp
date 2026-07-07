#include "SequenceEngine.h"

#include <Arduino.h>

namespace {
// -------------------------------------------------------------------------
// CHOREOGRAPHY — from "Sequence - Sheet1 (1).csv" (rise 4 s, hold 56 s).
//
// The arcs wake one at a time: each RISES over 4 s, then everything HOLDS for
// 56 s before the next change; after all six are on they fall away one at a
// time the same way, and the loop closes at 668 s. Each step is therefore a
// pair of rows: the ramp target, then the same frame again at +56 s (the hold).
// Values are the SCALE applied to each channel's stored set-point: F (255) =
// the set-point itself (the CSV's "80%" rows), 0 = off. Times are ms from
// start. Keep the table sorted by timeMs.
// -------------------------------------------------------------------------
constexpr uint8_t F = 255;
const Cue kSequenceCues[] = {
  //  t(ms)     bulb scale [0..5]     motor scale [0..5]
  {       0, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } },
  {    4000, { F, 0, 0, 0, 0, 0 }, { F, 0, 0, 0, 0, 0 } },   // arc 1 rises
  {   60000, { F, 0, 0, 0, 0, 0 }, { F, 0, 0, 0, 0, 0 } },   //   hold
  {   64000, { F, F, 0, 0, 0, 0 }, { F, F, 0, 0, 0, 0 } },   // arc 2 rises
  {  120000, { F, F, 0, 0, 0, 0 }, { F, F, 0, 0, 0, 0 } },   //   hold
  {  124000, { F, F, F, 0, 0, 0 }, { F, F, F, 0, 0, 0 } },   // arc 3 rises
  {  180000, { F, F, F, 0, 0, 0 }, { F, F, F, 0, 0, 0 } },   //   hold
  {  184000, { F, F, F, F, 0, 0 }, { F, F, F, F, 0, 0 } },   // arc 4 rises
  {  240000, { F, F, F, F, 0, 0 }, { F, F, F, F, 0, 0 } },   //   hold
  {  244000, { F, F, F, F, F, 0 }, { F, F, F, F, F, 0 } },   // arc 5 rises
  {  300000, { F, F, F, F, F, 0 }, { F, F, F, F, F, 0 } },   //   hold
  {  304000, { F, F, F, F, F, F }, { F, F, F, F, F, F } },   // arc 6 rises — all on
  {  360000, { F, F, F, F, F, F }, { F, F, F, F, F, F } },   //   hold
  {  364000, { 0, F, F, F, F, F }, { 0, F, F, F, F, F } },   // arc 1 falls
  {  420000, { 0, F, F, F, F, F }, { 0, F, F, F, F, F } },   //   hold
  {  424000, { 0, 0, F, F, F, F }, { 0, 0, F, F, F, F } },   // arc 2 falls
  {  480000, { 0, 0, F, F, F, F }, { 0, 0, F, F, F, F } },   //   hold
  {  484000, { 0, 0, 0, F, F, F }, { 0, 0, 0, F, F, F } },   // arc 3 falls
  {  540000, { 0, 0, 0, F, F, F }, { 0, 0, 0, F, F, F } },   //   hold
  {  544000, { 0, 0, 0, 0, F, F }, { 0, 0, 0, 0, F, F } },   // arc 4 falls
  {  600000, { 0, 0, 0, 0, F, F }, { 0, 0, 0, 0, F, F } },   //   hold
  {  604000, { 0, 0, 0, 0, 0, F }, { 0, 0, 0, 0, 0, F } },   // arc 5 falls
  {  660000, { 0, 0, 0, 0, 0, F }, { 0, 0, 0, 0, 0, F } },   //   hold
  {  664000, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } },   // arc 6 falls — all dark
  {  668000, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } },   // END: closes the loop
};
constexpr uint16_t kSequenceCount = sizeof(kSequenceCues) / sizeof(kSequenceCues[0]);

uint8_t lerp8(uint8_t a, uint8_t b, uint32_t num, uint32_t den) {
  if (den == 0) return a;
  int32_t d = (int32_t)b - (int32_t)a;
  return (uint8_t)((int32_t)a + d * (int32_t)num / (int32_t)den);
}

// value * scale / 255, rounded.
inline uint8_t scale8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * scale + 127) / 255);
}
}  // namespace

void SequenceEngine::begin() {
  cues_  = kSequenceCues;
  count_ = kSequenceCount;
}

void SequenceEngine::load(const Cue* cues, uint16_t count) {
  if (cues && count >= 1) {
    cues_  = cues;
    count_ = count;
  }
}

void SequenceEngine::start() {
  running_ = true;
  startMs_ = millis();
}

void SequenceEngine::stop() {
  // SEQUENCE_STOP_HOLD just controls who writes the targets after we stop: the
  // main loop holds the last frame (true) or reverts to the gallery set-points
  // (false). Either way the engine simply ceases producing frames.
  running_ = false;
}

uint32_t SequenceEngine::positionMs() const {
  if (!running_ || count_ == 0) return 0;
  uint32_t loopLen = loopLengthMs();
  return loopLen ? (millis() - startMs_) % loopLen : 0;
}

bool SequenceEngine::fill(const uint8_t setBrightness[NUM_CHANNELS],
                          const uint8_t setSpeed[NUM_CHANNELS],
                          uint8_t outBrightness[NUM_CHANNELS],
                          uint8_t outSpeed[NUM_CHANNELS]) {
  if (!running_ || !cues_ || count_ == 0) return false;

  uint32_t t = positionMs();

  // Find the segment [a, b] with cues_[a].timeMs <= t < cues_[b].timeMs.
  uint16_t a = 0;
  while (a + 1 < count_ && cues_[a + 1].timeMs <= t) ++a;
  uint16_t b = (a + 1 < count_) ? a + 1 : a;

  uint32_t segStart = cues_[a].timeMs;
  uint32_t segEnd   = cues_[b].timeMs;
  uint32_t num = t - segStart;
  uint32_t den = (segEnd > segStart) ? (segEnd - segStart) : 0;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    // Interpolate the cue SCALE, then apply it to the commissioned set-point.
    uint8_t bs = lerp8(cues_[a].brightness[ch], cues_[b].brightness[ch], num, den);
    uint8_t ss = lerp8(cues_[a].speed[ch],      cues_[b].speed[ch],      num, den);
    outBrightness[ch] = scale8(setBrightness[ch], bs);
    outSpeed[ch]      = scale8(setSpeed[ch],      ss);
  }
  return true;
}
