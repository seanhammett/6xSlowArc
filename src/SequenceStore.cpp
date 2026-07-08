#include "SequenceStore.h"

#include <Preferences.h>
#include <string.h>
#include <stdio.h>

namespace {
const char* kNs = "seqs";

void keyFor(uint8_t slot, char out[4]) { snprintf(out, 4, "s%u", slot); }

// -------------------------------------------------------------------------
// Built-in piece — "Sequence - Sheet1 (1).csv": rise 4 s, hold 56 s. The arcs
// wake one at a time, all six hold, then fall away in the same order; the loop
// closes at 668 s (last step + ramp). Seeded into slot 0 on first boot; from
// then on the artist edits/saves sequences from the web UI.
// -------------------------------------------------------------------------
const SeqDef kDefaultSeq = {
  "Original",
  4000,                              // shared rise/fall (ms)
  12,
  {
    //  t(ms)  arcs on (bit 0 = arc 1)
    {   4000, 0b000001 },
    {  64000, 0b000011 },
    { 124000, 0b000111 },
    { 184000, 0b001111 },
    { 244000, 0b011111 },
    { 304000, 0b111111 },
    { 364000, 0b111110 },
    { 424000, 0b111100 },
    { 484000, 0b111000 },
    { 544000, 0b110000 },
    { 604000, 0b100000 },
    { 664000, 0b000000 },
  }
};
}  // namespace

void SequenceStore::begin() {
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  bool any = false;
  char k[4];
  for (uint8_t i = 0; i < SEQ_SLOTS && !any; ++i) {
    keyFor(i, k);
    any = p.isKey(k);
  }
  if (!any) {
    keyFor(0, k);
    p.putBytes(k, &kDefaultSeq, sizeof(SeqDef));
    p.putUChar("act", 0);
  }
  if (!p.isKey("act")) p.putUChar("act", 0);
  p.end();
}

bool SequenceStore::exists(uint8_t slot) const {
  if (slot >= SEQ_SLOTS) return false;
  Preferences p;
  if (!p.begin(kNs, /*readOnly=*/true)) return false;
  char k[4];
  keyFor(slot, k);
  bool ok = p.isKey(k);
  p.end();
  return ok;
}

bool SequenceStore::load(uint8_t slot, SeqDef& out) const {
  if (slot >= SEQ_SLOTS) return false;
  Preferences p;
  if (!p.begin(kNs, /*readOnly=*/true)) return false;
  char k[4];
  keyFor(slot, k);
  // Size mismatch (e.g. after a struct-layout change) reads as absent.
  bool ok = p.isKey(k) && p.getBytes(k, &out, sizeof(SeqDef)) == sizeof(SeqDef);
  p.end();
  if (ok) {
    out.name[sizeof(out.name) - 1] = '\0';
    if (out.stepCount > SEQ_MAX_STEPS) ok = false;
  }
  return ok;
}

bool SequenceStore::save(uint8_t slot, const SeqDef& def) {
  if (slot >= SEQ_SLOTS) return false;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  char k[4];
  keyFor(slot, k);
  bool ok = p.putBytes(k, &def, sizeof(SeqDef)) == sizeof(SeqDef);
  p.end();
  return ok;
}

int SequenceStore::saveByName(const SeqDef& def) {
  int freeSlot = -1;
  SeqDef existing;
  for (uint8_t i = 0; i < SEQ_SLOTS; ++i) {
    if (load(i, existing)) {
      if (strncmp(existing.name, def.name, sizeof(existing.name)) == 0) {
        return save(i, def) ? i : -1;              // overwrite same name
      }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }
  if (freeSlot < 0) return -1;                      // all slots taken
  return save((uint8_t)freeSlot, def) ? freeSlot : -1;
}

uint8_t SequenceStore::activeIndex() const {
  Preferences p;
  if (!p.begin(kNs, /*readOnly=*/true)) return 0;
  uint8_t a = p.getUChar("act", 0);
  p.end();
  return (a < SEQ_SLOTS) ? a : 0;
}

void SequenceStore::setActive(uint8_t slot) {
  if (slot >= SEQ_SLOTS) return;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putUChar("act", slot);
  p.end();
}
