#include "SequenceEngine.h"

#include <Arduino.h>

namespace {
// -------------------------------------------------------------------------
// PLACEHOLDER CHOREOGRAPHY — replace with the artist's piece.
//
// A slow ~24s loop: a brightness/speed "swell" walks across the six arcs. Each
// row is a keyframe; the engine eases between rows and loops back to the first.
// Times are ms from start. Keep the table sorted by timeMs.
// -------------------------------------------------------------------------
const Cue kPlaceholderCues[] = {
  //  t(ms)   brightness[0..5]            speed[0..5]
  {     0, { 30, 30, 30, 30, 30, 30 }, { 20, 20, 20, 20, 20, 20 } },
  {  4000, {255, 60, 30, 30, 30, 30 }, {120, 40, 20, 20, 20, 20 } },
  {  8000, { 60,255, 60, 30, 30, 30 }, { 40,120, 40, 20, 20, 20 } },
  { 12000, { 30, 60,255, 60, 30, 30 }, { 20, 40,120, 40, 20, 20 } },
  { 16000, { 30, 30, 60,255, 60, 30 }, { 20, 20, 40,120, 40, 20 } },
  { 20000, { 30, 30, 30, 60,255, 60 }, { 20, 20, 20, 40,120, 40 } },
  { 24000, { 30, 30, 30, 30, 30, 30 }, { 20, 20, 20, 20, 20, 20 } },  // == row 0, closes the loop
};
constexpr uint16_t kPlaceholderCount = sizeof(kPlaceholderCues) / sizeof(kPlaceholderCues[0]);

uint8_t lerp8(uint8_t a, uint8_t b, uint32_t num, uint32_t den) {
  if (den == 0) return a;
  int32_t d = (int32_t)b - (int32_t)a;
  return (uint8_t)((int32_t)a + d * (int32_t)num / (int32_t)den);
}
}  // namespace

void SequenceEngine::begin() {
  cues_  = kPlaceholderCues;
  count_ = kPlaceholderCount;
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

bool SequenceEngine::fill(uint8_t outBrightness[NUM_CHANNELS], uint8_t outSpeed[NUM_CHANNELS]) {
  if (!running_ || !cues_ || count_ == 0) return false;

  uint32_t loopLen = cues_[count_ - 1].timeMs;
  uint32_t t = (loopLen > 0) ? (millis() - startMs_) % loopLen : 0;

  // Find the segment [a, b] with cues_[a].timeMs <= t < cues_[b].timeMs.
  uint16_t a = 0;
  while (a + 1 < count_ && cues_[a + 1].timeMs <= t) ++a;
  uint16_t b = (a + 1 < count_) ? a + 1 : a;

  uint32_t segStart = cues_[a].timeMs;
  uint32_t segEnd   = cues_[b].timeMs;
  uint32_t num = t - segStart;
  uint32_t den = (segEnd > segStart) ? (segEnd - segStart) : 0;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    outBrightness[ch] = lerp8(cues_[a].brightness[ch], cues_[b].brightness[ch], num, den);
    outSpeed[ch]      = lerp8(cues_[a].speed[ch],      cues_[b].speed[ch],      num, den);
  }
  return true;
}
