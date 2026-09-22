#include "SequenceEngine.h"

#include <Arduino.h>
#include <string.h>

namespace {
// Guards def_ against the web task's snapshot() while apply()/start() replace
// it. The copy is ~8 KB — a few microseconds inside the critical section.
portMUX_TYPE defMux = portMUX_INITIALIZER_UNLOCKED;

// value * scale / 255, rounded.
inline uint8_t scale8(uint8_t value, uint8_t scale) {
  return (uint8_t)(((uint16_t)value * scale + 127) / 255);
}

// The same scaling for a step rate, which is Hz and needs the wider intermediate.
inline uint16_t scaleHz(uint16_t hz, uint8_t scale) {
  return (uint16_t)(((uint32_t)hz * scale + 127) / 255);
}
}  // namespace

void SequenceEngine::begin() {
  // No choreography yet — main applies the active stored sequence right after
  // (SequenceStore seeds the built-in piece on first boot).
}

void SequenceEngine::setDef(const SeqDef& def) {
  portENTER_CRITICAL(&defMux);
  memcpy(&def_, &def, sizeof(SeqDef));
  if (def_.stepCount > SEQ_MAX_STEPS) def_.stepCount = 0;   // never trust a bad count
  def_.name[sizeof(def_.name) - 1] = '\0';
  loopLen_ = seqLoopLengthMs(def_);
  portEXIT_CRITICAL(&defMux);
}

void SequenceEngine::snapshot(SeqDef& out) const {
  portENTER_CRITICAL(&defMux);
  memcpy(&out, &def_, sizeof(SeqDef));
  portEXIT_CRITICAL(&defMux);
}

void SequenceEngine::apply(const SeqDef& def) {
  if (running_) {                                  // takes effect on next start()
    queued_    = def;
    hasQueued_ = true;
  } else {
    setDef(def);
  }
}

void SequenceEngine::start() {
  if (hasQueued_) {
    setDef(queued_);
    hasQueued_ = false;
  }
  ++runId_;
  running_ = true;
  startMs_ = millis();
}

void SequenceEngine::stop() {
  // SEQUENCE_STOP_HOLD just controls who writes the targets after we stop: the
  // main loop holds the last frame (true) or reverts to the gallery set-points
  // (false). Either way the engine simply ceases producing frames.
  running_ = false;
}

void SequenceEngine::nudge(int32_t ms) {
  if (!running_) return;
  uint32_t elapsed = millis() - startMs_;
  if (ms < 0 && (uint32_t)(-ms) > elapsed) ms = -(int32_t)elapsed;
  startMs_ -= (uint32_t)ms;                        // earlier start = later position
}

uint32_t SequenceEngine::positionMs() const {
  uint32_t loopLen = loopLen_;
  if (!running_ || loopLen == 0) return 0;
  return (millis() - startMs_) % loopLen;
}

bool SequenceEngine::fill(const uint8_t  setBrightness[NUM_CHANNELS],
                          const uint16_t setSpeedHz[NUM_CHANNELS],
                          uint8_t  outBrightness[NUM_CHANNELS],
                          uint16_t outSpeedHz[NUM_CHANNELS]) {
  if (!running_) return false;

  uint8_t scale[NUM_CHANNELS];
  if (!seqFrameAt(def_, positionMs(), scale)) return false;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) {
    // Bulb and motor share one scale: an arc's bulb + motor move together.
    outBrightness[ch] = scale8(setBrightness[ch], scale[ch]);
    outSpeedHz[ch]    = scaleHz(setSpeedHz[ch],   scale[ch]);
  }
  return true;
}
