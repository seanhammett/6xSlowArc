#include "SequenceStore.h"

#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace {
const char*    kNs    = "seqs";                  // NVS: active index (+ legacy blobs)
const char*    kDir   = "/seq";
const uint32_t kMagic = 0x32514553;              // "SEQ2" little-endian

// On-flash header, followed by stepCount raw SeqStep records.
struct FileHeader {
  uint32_t magic;
  uint16_t stepCount;
  uint16_t reserved;
  uint32_t rampMs;
  char     name[sizeof(SeqDef::name)];
};

void pathFor(uint8_t slot, char out[16], const char* ext = "bin") {
  snprintf(out, 16, "%s/%u.%s", kDir, slot, ext);
}

// Read and sanity-check a slot's header, leaving f positioned at the steps.
bool openSlot(uint8_t slot, File& f, FileHeader& h) {
  if (slot >= SEQ_SLOTS) return false;
  char p[16];
  pathFor(slot, p);
  if (!LittleFS.exists(p)) return false;
  f = LittleFS.open(p, "r");
  if (!f) return false;
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) &&
            h.magic == kMagic && h.stepCount <= SEQ_MAX_STEPS &&
            f.size() >= sizeof(h) + (size_t)h.stepCount * sizeof(SeqStep);
  if (!ok) { f.close(); return false; }
  h.name[sizeof(h.name) - 1] = '\0';
  return true;
}

// -------------------------------------------------------------------------
// The NVS layout older firmware stored (SEQ_MAX_STEPS was 32, stepCount a
// uint8_t). Only read, once, to migrate those sequences onto LittleFS.
// -------------------------------------------------------------------------
struct LegacySeqDef {
  char     name[24];
  uint32_t rampMs;
  uint8_t  stepCount;
  SeqStep  steps[32];
};
static_assert(sizeof(LegacySeqDef) == 288, "must match the old NVS blob size");

void keyFor(uint8_t slot, char out[4]) { snprintf(out, 4, "s%u", slot); }

// -------------------------------------------------------------------------
// Built-in piece — "Sequence - Sheet1 (1).csv": rise 4 s, hold 56 s. The arcs
// wake one at a time, all six hold, then fall away in the same order; the loop
// closes at 668 s (last step + ramp). Seeded into slot 0 on a fresh box; from
// then on the artist edits/saves sequences from the web UI.
// -------------------------------------------------------------------------
const struct { uint32_t t; uint8_t m; } kDefaultSteps[] = {
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
};

void printEscaped(Print& out, const char* s) {
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') out.write('\\');
    out.write(*s);
  }
}
}  // namespace

void printSeqJson(Print& out, const SeqDef& d) {
  out.print("{\"name\":\"");
  printEscaped(out, d.name);
  out.print("\",\"ramp_ms\":"); out.print(d.rampMs);
  out.print(",\"len\":");       out.print(seqLoopLengthMs(d));
  out.print(",\"steps\":[");
  for (uint16_t i = 0; i < d.stepCount; ++i) {
    if (i) out.write(',');
    out.write('['); out.print(d.steps[i].timeMs);
    out.write(','); out.print(d.steps[i].mask); out.write(']');
  }
  out.print("]}");
}

void SequenceStore::begin() {
  // The partition label is "spiffs" in the stock tables; LittleFS formats it on
  // first mount (it held nothing before).
  mounted_ = LittleFS.begin(/*formatOnFail=*/true, "/littlefs", 4, "spiffs");
  if (!mounted_) {
    Serial.println("[seq] LittleFS mount failed — sequences unavailable");
    return;
  }
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);

  bool any = false;
  for (uint8_t i = 0; i < SEQ_SLOTS && !any; ++i) any = exists(i);

  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  if (!any) {
    // Heap, not stack: the loop task's stack is 8 KB and a SeqDef is about that.
    SeqDef* d = new (std::nothrow) SeqDef();
    LegacySeqDef old;
    char k[4];
    uint8_t migrated = 0;
    for (uint8_t i = 0; d && i < SEQ_SLOTS; ++i) {
      keyFor(i, k);
      if (!p.isKey(k)) continue;
      // Size mismatch (a still older layout) reads as absent, as it always did.
      if (p.getBytes(k, &old, sizeof(old)) == sizeof(old) && old.stepCount <= 32) {
        memset(d, 0, sizeof(SeqDef));
        memcpy(d->name, old.name, sizeof(d->name));
        d->name[sizeof(d->name) - 1] = '\0';
        d->rampMs    = old.rampMs;
        d->stepCount = old.stepCount;
        memcpy(d->steps, old.steps, old.stepCount * sizeof(SeqStep));
        // The old blob is left in NVS (~2.3 KB for all eight): flashing the
        // previous firmware back then still finds the artist's sequences.
        if (save(i, *d)) ++migrated;
      }
    }
    if (d && migrated == 0) {
      memset(d, 0, sizeof(SeqDef));
      strlcpy(d->name, "Original", sizeof(d->name));
      d->rampMs    = 4000;                     // shared rise/fall (ms)
      d->stepCount = sizeof(kDefaultSteps) / sizeof(kDefaultSteps[0]);
      for (uint16_t s = 0; s < d->stepCount; ++s) {
        d->steps[s].timeMs = kDefaultSteps[s].t;
        d->steps[s].mask   = kDefaultSteps[s].m;
      }
      save(0, *d);
    }
    if (migrated) Serial.printf("[seq] migrated %u sequence(s) from NVS\n", migrated);
    delete d;
  }
  if (!p.isKey("act")) p.putUChar("act", 0);
  p.end();
}

bool SequenceStore::exists(uint8_t slot) const {
  if (!mounted_ || slot >= SEQ_SLOTS) return false;
  char p[16];
  pathFor(slot, p);
  return LittleFS.exists(p);
}

bool SequenceStore::load(uint8_t slot, SeqDef& out) const {
  if (!mounted_) return false;
  File f;
  FileHeader h;
  if (!openSlot(slot, f, h)) return false;
  memset(&out, 0, sizeof(SeqDef));
  memcpy(out.name, h.name, sizeof(out.name));
  out.rampMs    = h.rampMs;
  out.stepCount = h.stepCount;
  size_t bytes = (size_t)h.stepCount * sizeof(SeqStep);
  bool ok = f.read((uint8_t*)out.steps, bytes) == bytes;
  f.close();
  if (!ok) out.stepCount = 0;
  return ok;
}

bool SequenceStore::nameOf(uint8_t slot, char out[sizeof(SeqDef::name)]) const {
  if (!mounted_) return false;
  File f;
  FileHeader h;
  if (!openSlot(slot, f, h)) return false;
  f.close();
  memcpy(out, h.name, sizeof(h.name));
  return true;
}

bool SequenceStore::streamJson(uint8_t slot, Print& out) const {
  if (!mounted_) return false;
  File f;
  FileHeader h;
  if (!openSlot(slot, f, h)) return false;
  uint32_t len = 0;
  // The loop length needs the last step, which is at the end of the file.
  if (h.stepCount) {
    SeqStep lastStep;
    f.seek(sizeof(h) + (size_t)(h.stepCount - 1) * sizeof(SeqStep));
    if (f.read((uint8_t*)&lastStep, sizeof(lastStep)) == sizeof(lastStep))
      len = lastStep.timeMs + h.rampMs;
    f.seek(sizeof(h));
  }
  out.print("{\"name\":\"");
  printEscaped(out, h.name);
  out.print("\",\"ramp_ms\":"); out.print(h.rampMs);
  out.print(",\"len\":");       out.print(len);
  out.print(",\"steps\":[");
  SeqStep s;
  for (uint16_t i = 0; i < h.stepCount; ++i) {
    if (f.read((uint8_t*)&s, sizeof(s)) != sizeof(s)) break;
    if (i) out.write(',');
    out.write('['); out.print(s.timeMs);
    out.write(','); out.print(s.mask); out.write(']');
  }
  out.print("]}");
  f.close();
  return true;
}

bool SequenceStore::save(uint8_t slot, const SeqDef& def) {
  if (!mounted_ || slot >= SEQ_SLOTS || def.stepCount > SEQ_MAX_STEPS) return false;
  char tmp[16], dst[16];
  pathFor(slot, tmp, "tmp");
  pathFor(slot, dst);

  FileHeader h = {};
  h.magic     = kMagic;
  h.stepCount = def.stepCount;
  h.rampMs    = def.rampMs;
  memcpy(h.name, def.name, sizeof(h.name));
  h.name[sizeof(h.name) - 1] = '\0';

  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t bytes = (size_t)def.stepCount * sizeof(SeqStep);
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) &&
            f.write((const uint8_t*)def.steps, bytes) == bytes;
  f.close();
  // Rename over the old file so the slot flips from old to new in one step.
  if (ok) ok = LittleFS.rename(tmp, dst);
  if (!ok) LittleFS.remove(tmp);
  return ok;
}

int SequenceStore::saveByName(const SeqDef& def) {
  int freeSlot = -1;
  char nm[sizeof(SeqDef::name)];
  for (uint8_t i = 0; i < SEQ_SLOTS; ++i) {
    if (nameOf(i, nm)) {
      if (strncmp(nm, def.name, sizeof(nm)) == 0) {
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
