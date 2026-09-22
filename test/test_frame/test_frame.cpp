// Host-side check that seqFrameAt() (evaluates the frame straight from the
// steps) matches, value for value, the cue-table engine it replaced: expand()
// into hold + target cues, then scan + lerp. The reference below is that old
// code, copied verbatim apart from widening the step counters to uint16_t.
//
//   pio test -e native

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SeqFrame.h"

namespace ref {
struct Cue {
  uint32_t timeMs;
  uint8_t  v[NUM_CHANNELS];
};

Cue      buf[2 * SEQ_MAX_STEPS + 2];
uint16_t count = 0;

void expand(const SeqDef& def) {
  uint16_t n = 0;
  auto put = [&](uint32_t t, uint8_t mask) {
    if (n && buf[n - 1].timeMs == t) --n;          // same-time dup: overwrite
    Cue& c = buf[n++];
    c.timeMs = t;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch) c.v[ch] = (mask >> ch) & 1 ? 255 : 0;
  };
  count = 0;
  if (def.stepCount == 0) return;
  uint8_t  lastMask = def.steps[def.stepCount - 1].mask;
  uint8_t  prevMask = lastMask;
  uint32_t prevT    = 0;
  put(0, lastMask);
  for (uint16_t i = 0; i < def.stepCount; ++i) {
    const SeqStep& s = def.steps[i];
    uint32_t hold = (s.timeMs > def.rampMs) ? s.timeMs - def.rampMs : 0;
    if (hold < prevT) hold = prevT;
    put(hold, prevMask);
    put(s.timeMs, s.mask);
    prevMask = s.mask;
    prevT    = s.timeMs;
  }
  put(prevT + def.rampMs, lastMask);
  count = n;
}

void frame(uint32_t t, uint8_t out[NUM_CHANNELS]) {
  uint16_t a = 0;
  while (a + 1 < count && buf[a + 1].timeMs <= t) ++a;
  uint16_t b = (a + 1 < count) ? a + 1 : a;
  uint32_t segStart = buf[a].timeMs, segEnd = buf[b].timeMs;
  uint32_t num = t - segStart;
  uint32_t den = (segEnd > segStart) ? (segEnd - segStart) : 0;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ++ch)
    out[ch] = seqframe::lerp8(buf[a].v[ch], buf[b].v[ch], num, den);
}
}  // namespace ref

static SeqDef def;

static void setSteps(uint32_t rampMs, const uint32_t* t, const uint8_t* m, uint16_t n) {
  memset(&def, 0, sizeof(def));
  def.rampMs = rampMs;
  def.stepCount = n;
  for (uint16_t i = 0; i < n; ++i) { def.steps[i].timeMs = t[i]; def.steps[i].mask = m[i]; }
}

// Compare at every `stride` ms across the whole loop, plus every cue boundary
// and the millisecond either side of it.
static void compareAll(uint32_t stride) {
  ref::expand(def);
  uint32_t len = seqLoopLengthMs(def);
  TEST_ASSERT_EQUAL_UINT32(ref::buf[ref::count - 1].timeMs, len);
  uint8_t a[NUM_CHANNELS], b[NUM_CHANNELS];
  auto check = [&](uint32_t t) {
    if (t >= len) return;
    ref::frame(t, a);
    TEST_ASSERT_TRUE(seqFrameAt(def, t, b));
    char msg[48];
    snprintf(msg, sizeof(msg), "t=%u ms", (unsigned)t);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(a, b, NUM_CHANNELS, msg);
  };
  for (uint32_t t = 0; t < len; t += stride) check(t);
  for (uint16_t i = 0; i < ref::count; ++i) {
    uint32_t c = ref::buf[i].timeMs;
    if (c) check(c - 1);
    check(c);
    check(c + 1);
  }
}

void test_original() {
  const uint32_t t[] = { 4000, 64000, 124000, 184000, 244000, 304000,
                         364000, 424000, 484000, 544000, 604000, 664000 };
  const uint8_t  m[] = { 1, 3, 7, 15, 31, 63, 62, 60, 56, 48, 32, 0 };
  setSteps(4000, t, m, 12);
  TEST_ASSERT_EQUAL_UINT32(668000, seqLoopLengthMs(def));
  compareAll(7);
}

void test_step_at_zero() {
  const uint32_t t[] = { 0, 5000, 9000 };
  const uint8_t  m[] = { 5, 10, 63 };
  setSteps(2000, t, m, 3);
  compareAll(3);
}

void test_single_step() {
  const uint32_t t[] = { 3000 };
  const uint8_t  m[] = { 42 };
  setSteps(1500, t, m, 1);
  compareAll(1);
}

void test_steps_closer_than_ramp() {
  const uint32_t t[] = { 1000, 1500, 1700, 6000, 6100 };
  const uint8_t  m[] = { 1, 2, 4, 63, 0 };
  setSteps(4000, t, m, 5);
  compareAll(1);
}

void test_duplicate_times() {
  const uint32_t t[] = { 2000, 2000, 5000, 8000, 8000, 8000, 12000 };
  const uint8_t  m[] = { 1, 7, 3, 9, 12, 33, 0 };
  setSteps(1000, t, m, 7);
  compareAll(1);
}

void test_1000_steps() {
  srand(1234);
  memset(&def, 0, sizeof(def));
  def.rampMs = 1500;
  def.stepCount = SEQ_MAX_STEPS;
  uint32_t t = 0;
  for (uint16_t i = 0; i < SEQ_MAX_STEPS; ++i) {
    t += 100 * (rand() % 50);                      // 0..4.9 s apart, dups included
    def.steps[i].timeMs = t;
    def.steps[i].mask   = rand() & 63;
  }
  compareAll(37);
}

void test_empty() {
  memset(&def, 0, sizeof(def));
  uint8_t out[NUM_CHANNELS];
  TEST_ASSERT_FALSE(seqFrameAt(def, 0, out));
  TEST_ASSERT_EQUAL_UINT32(0, seqLoopLengthMs(def));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_original);
  RUN_TEST(test_step_at_zero);
  RUN_TEST(test_single_step);
  RUN_TEST(test_steps_closer_than_ramp);
  RUN_TEST(test_duplicate_times);
  RUN_TEST(test_1000_steps);
  RUN_TEST(test_empty);
  return UNITY_END();
}
