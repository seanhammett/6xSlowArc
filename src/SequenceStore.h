#pragma once
//
// SequenceStore — persists artist-editable sequences (SeqDef) to NVS.
//
// SEQ_SLOTS numbered slots under one namespace, each holding a raw SeqDef blob,
// plus the index of the ACTIVE sequence (the one the sequence button plays).
// First boot seeds slot 0 with the built-in piece ("Original", from the
// artist's CSV) so the web UI's dropdown is never empty.
//
// Writes happen only from web-UI actions (save / select), so unlike ConfigStore
// there is no debouncing — each action is one NVS commit.
//

#include "SequenceEngine.h"   // SeqDef

class SequenceStore {
 public:
  void begin();                              // seed slot 0 + active on first boot

  bool exists(uint8_t slot) const;
  bool load(uint8_t slot, SeqDef& out) const;
  bool save(uint8_t slot, const SeqDef& def);

  // Overwrite the slot whose stored name matches def.name, else the first free
  // slot. Returns the slot used, or -1 if every slot is taken by another name.
  int saveByName(const SeqDef& def);

  uint8_t activeIndex() const;
  void    setActive(uint8_t slot);
};
