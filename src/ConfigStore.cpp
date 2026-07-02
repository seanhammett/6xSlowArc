#include "ConfigStore.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {
constexpr char     NVS_NAMESPACE[] = "slowarc";
constexpr char     NVS_KEY_BLOB[]  = "cfg";
constexpr uint8_t  BLOB_VERSION    = 1;

// On-flash layout. Packed and versioned.
struct __attribute__((packed)) StoredConfig {
  uint8_t version;
  uint8_t brightness[NUM_CHANNELS];
  uint8_t speed[NUM_CHANNELS];
  uint8_t mode;
};
}  // namespace

void ConfigStore::begin(ChannelModel& model) {
  model_ = &model;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  StoredConfig blob{};
  size_t read = prefs.getBytes(NVS_KEY_BLOB, &blob, sizeof(blob));
  prefs.end();

  if (read == sizeof(blob) && blob.version == BLOB_VERSION) {
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      model_->brightness[i] = blob.brightness[i];
      model_->speed[i]      = blob.speed[i];
    }
    model_->mode = (blob.mode == static_cast<uint8_t>(Mode::Performance))
                       ? Mode::Performance
                       : Mode::Gallery;
  } else {
    // Virgin device or layout change: start from defaults and write them back so
    // subsequent boots are clean.
    model_->loadDefaults();
    save();
  }

  dirty_ = false;
}

void ConfigStore::markDirty() {
  dirty_        = true;
  lastChangeMs_ = millis();
}

void ConfigStore::tick() {
  if (!dirty_) return;
  if (millis() - lastChangeMs_ < NVS_SAVE_DEBOUNCE_MS) return;
  save();
}

void ConfigStore::flush() {
  if (dirty_) save();
}

void ConfigStore::save() {
  if (!model_) return;

  StoredConfig blob{};
  blob.version = BLOB_VERSION;
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
    blob.brightness[i] = model_->brightness[i];
    blob.speed[i]      = model_->speed[i];
  }
  blob.mode = static_cast<uint8_t>(model_->mode);

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putBytes(NVS_KEY_BLOB, &blob, sizeof(blob));
  prefs.end();

  dirty_ = false;
}
