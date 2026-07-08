#include "SequenceEngine.h"

#include <Arduino.h>

namespace {
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
  // No choreography yet — main applies the active stored sequence right after
  // (SequenceStore seeds NVS with the built-in piece on first boot).
}

// Expand steps + shared ramp into the cue table. Each step contributes a
// hold-until cue at (t - ramp) with the previous state and a target cue at t;
// t=0 carries the last step's state so the loop is seamless, and the loop
// closes at lastT + ramp (the CSV's END row).
void SequenceEngine::expand(const SeqDef& def) {
  uint16_t n = 0;
  auto put = [&](uint32_t t, uint8_t mask) {
    if (n && buf_[n - 1].timeMs == t) --n;         // same-time dup: overwrite
    Cue& c = buf_[n++];
    c.timeMs = t;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
      uint8_t v = (mask >> ch) & 1 ? 255 : 0;      // bulb + motor move together
      c.brightness[ch] = v;
      c.speed[ch]      = v;
    }
  };

  if (def.stepCount == 0) { cues_ = nullptr; count_ = 0; return; }

  uint8_t  lastMask = def.steps[def.stepCount - 1].mask;
  uint8_t  prevMask = lastMask;
  uint32_t prevT    = 0;
  put(0, lastMask);                                // loop-seamless start state
  for (uint8_t i = 0; i < def.stepCount; ++i) {
    const SeqStep& s = def.steps[i];
    uint32_t hold = (s.timeMs > def.rampMs) ? s.timeMs - def.rampMs : 0;
    if (hold < prevT) hold = prevT;                // ramp squeezed by close steps
    put(hold, prevMask);
    put(s.timeMs, s.mask);
    prevMask = s.mask;
    prevT    = s.timeMs;
  }
  put(prevT + def.rampMs, lastMask);               // END: closes the loop

  cues_  = buf_;
  count_ = n;
}

void SequenceEngine::apply(const SeqDef& def) {
  if (running_) {                                  // takes effect on next start()
    queued_    = def;
    hasQueued_ = true;
  } else {
    expand(def);
  }
}

void SequenceEngine::start() {
  if (hasQueued_) {
    expand(queued_);
    hasQueued_ = false;
  }
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
