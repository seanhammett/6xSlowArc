#include "ConfigStore.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {
constexpr char     NVS_NAMESPACE[] = "slowarc";
constexpr char     NVS_KEY_BLOB[]  = "cfg";
constexpr uint8_t  BLOB_VERSION    = 2;

// On-flash layout. Packed and versioned.
struct __attribute__((packed)) StoredConfig {
  uint8_t  version;
  uint8_t  brightness[NUM_CHANNELS];
  uint16_t speedHz[NUM_CHANNELS];
  uint8_t  mode;
};

// v1 held speed as a 0..255 set-point instead of Hz. A box commissioned on that
// firmware still has one in flash, so read it and convert rather than dropping
// the installer back to defaults.
struct __attribute__((packed)) StoredConfigV1 {
  uint8_t version;
  uint8_t brightness[NUM_CHANNELS];
  uint8_t speed[NUM_CHANNELS];
  uint8_t mode;
};

// The v1 mapping, against the ceiling that was in force when it was written —
// so an arc keeps the rate it was actually running, not a rescaled one.
uint16_t legacySpeedToHz(uint8_t value) {
  if (value == 0) return 0;
  constexpr uint32_t span = MOTOR_LEGACY_MAX_SPEED_HZ - MOTOR_MIN_SPEED_HZ;
  return (uint16_t)(MOTOR_MIN_SPEED_HZ + (uint32_t)(span * (value - 1) / 254));
}

Mode modeFromByte(uint8_t b) {
  return (b == static_cast<uint8_t>(Mode::Performance)) ? Mode::Performance : Mode::Gallery;
}
}  // namespace

void ConfigStore::begin(ChannelModel& model) {
  model_ = &model;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  uint8_t raw[sizeof(StoredConfig)] = {};      // the largest layout we understand
  size_t  read = prefs.getBytes(NVS_KEY_BLOB, raw, sizeof(raw));
  prefs.end();

  const uint8_t version = read ? raw[0] : 0;

  if (read == sizeof(StoredConfig) && version == BLOB_VERSION) {
    StoredConfig blob;
    memcpy(&blob, raw, sizeof(blob));          // packed layout: copy, don't alias
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      model_->brightness[i] = blob.brightness[i];
      model_->speedHz[i]    = blob.speedHz[i];
    }
    model_->mode = modeFromByte(blob.mode);
  } else if (read == sizeof(StoredConfigV1) && version == 1) {
    StoredConfigV1 blob;
    memcpy(&blob, raw, sizeof(blob));
    for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
      model_->brightness[i] = blob.brightness[i];
      model_->speedHz[i]    = legacySpeedToHz(blob.speed[i]);
    }
    model_->mode = modeFromByte(blob.mode);
    save();                                    // rewrite in the current layout
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

  // Clear before the snapshot, not after the write: a web write landing while
  // this commit is in flight re-marks the store and is saved on a later tick.
  dirty_ = false;

  StoredConfig blob{};
  blob.version = BLOB_VERSION;
  for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
    blob.brightness[i] = model_->brightness[i];
    blob.speedHz[i]    = model_->speedHz[i];
  }
  blob.mode = static_cast<uint8_t>(model_->mode);

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putBytes(NVS_KEY_BLOB, &blob, sizeof(blob));
  prefs.end();
}
