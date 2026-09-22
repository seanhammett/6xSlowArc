#pragma once
//
// SequenceStore — persists artist-editable sequences (SeqDef).
//
// SEQ_SLOTS numbered slots, one file each on the LittleFS "spiffs" partition
// (/seq/<slot>.bin), plus the index of the ACTIVE sequence (the one the sequence
// button plays), which stays in NVS. At SEQ_MAX_STEPS = 1000 a sequence is ~8 KB
// and eight of them no longer fit the 20 KB NVS partition; the 1.5 MB spiffs
// partition in the stock 8 MB table was unused, so no partition change is needed.
//
// First boot on this layout migrates any sequences the older firmware kept as
// NVS blobs (left in place, so the old firmware can still be flashed back), or
// — on a fresh box — seeds slot 0 with the built-in piece
// ("Original", from the artist's CSV) so the web UI's dropdown is never empty.
//
// Saves write a temp file and rename it over the slot, so a reader on the other
// task never sees a half-written sequence. Writes happen only from web-UI
// actions (save / select), so there is no debouncing.
//
// SeqDef is ~8 KB: callers pass heap or static buffers, never stack ones.
//

#include <Print.h>
#include "SeqFrame.h"   // SeqDef

class SequenceStore {
 public:
  void begin();                              // mount, migrate or seed on first boot

  bool exists(uint8_t slot) const;
  bool load(uint8_t slot, SeqDef& out) const;
  bool save(uint8_t slot, const SeqDef& def);

  // Just the stored name (reads the header only). False if the slot is empty.
  bool nameOf(uint8_t slot, char out[sizeof(SeqDef::name)]) const;

  // The slot as the web API's JSON, streamed from the file:
  // {"name":..,"len":loop_ms,"steps":[[t_ms,mask,ramp_ms],...]}
  bool streamJson(uint8_t slot, Print& out) const;

  // Overwrite the slot whose stored name matches def.name, else the first free
  // slot. Returns the slot used, or -1 if every slot is taken by another name.
  int saveByName(const SeqDef& def);

  uint8_t activeIndex() const;
  void    setActive(uint8_t slot);

  bool mounted() const { return mounted_; }

 private:
  bool mounted_ = false;
};

// The web API's sequence JSON, shared by SequenceStore::streamJson and the
// engine snapshot served at /api/sequence so both read the same way.
void printSeqJson(Print& out, const SeqDef& def);
