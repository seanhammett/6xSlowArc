#pragma once
//
// ConfigStore — persists the 12 set-points and the mode to NVS (Preferences).
//
// Persistence model
// -----------------
//   * NVS is the single source of persisted state (spec §8).
//   * On boot, load() restores the last-saved ChannelModel; if the namespace is
//     empty (virgin device) it falls back to ChannelModel::loadDefaults().
//   * Web-UI writes mark the store dirty; tick() coalesces them and commits once
//     the values have settled (NVS_SAVE_DEBOUNCE_MS) so a slider drag does not
//     hammer flash.
//
// Layout: a single binary blob under one key. Versioned so the layout can grow
// without bricking an existing install.
//

#include "ChannelModel.h"

class ConfigStore {
 public:
  void begin(ChannelModel& model);   // restore from NVS into model (or defaults)

  // Call when the model has changed and should eventually be persisted.
  void markDirty();

  // Pump from the main loop; performs the debounced commit when due.
  void tick();

  // Force an immediate commit (e.g. before a deliberate restart). Safe to call
  // even if nothing is dirty.
  void flush();

  bool isDirty() const { return dirty_; }

 private:
  void save();

  ChannelModel* model_ = nullptr;
  bool          dirty_ = false;
  uint32_t      lastChangeMs_ = 0;
};
