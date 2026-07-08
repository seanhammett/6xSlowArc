#pragma once
//
// WebUi — the WiFi status/control page (spec §6).
//
// Hosts an async HTTP server exposing:
//   GET  /                 the control/status page (sliders + live status)
//   GET  /api/state        JSON snapshot of the 12 set-points + system state
//   POST /api/set          write one channel's brightness and/or speed
//   POST /api/arc          per-arc on/off kill switch (runtime only, not saved)
//   GET  /api/sequence     the engine's current cue table (timeline view)
//   GET  /api/seqs         list stored sequences + the active slot
//   GET  /api/seq?i=N      one stored sequence definition (steps, seconds)
//   POST /api/seq/select   make slot N active (plays on next button push)
//   POST /api/seq/save     save a sequence (name, ramp, steps)
//   GET  /api/scan         async WiFi scan (202 while running, 200 + list done)
//   POST /api/wifi         store station credentials (forwarded via callback)
// Unknown paths redirect to / while the SoftAP is up (captive portal).
//
// Writes update ChannelModel and mark ConfigStore dirty (debounced persistence
// happens in the main loop). The page is a configuration surface, not a realtime
// control loop — the box runs entirely without it (WiFi optional, spec §8).
//
// The full system snapshot is assembled by the main module (it alone sees every
// subsystem) and handed in as a callback, keeping this module decoupled from the
// controllers.
//

#include <Arduino.h>
#include <functional>

#include "ChannelModel.h"
#include "ConfigStore.h"
#include "SequenceStore.h"

class WebUi {
 public:
  // stateJson must return a complete JSON object describing live system state;
  // sequenceJson the engine's cue table. arcEnabled is main's runtime per-arc
  // kill-switch array (NUM_CHANNELS entries), written by POST /api/arc.
  // onWifiCredentials receives (ssid, pass) from the WiFi setup card.
  // onSequenceActivated fires when the active sequence changes (selection, or
  // a save that touches the active slot) so main can hand it to the engine.
  void begin(ChannelModel* model, ConfigStore* store, SequenceStore* seqStore,
             bool* arcEnabled,
             std::function<String()> stateJson,
             std::function<String()> sequenceJson,
             std::function<void(const String&, const String&)> onWifiCredentials,
             std::function<void(uint8_t)> onSequenceActivated);

 private:
  ChannelModel*           model_ = nullptr;
  ConfigStore*            store_ = nullptr;
  SequenceStore*          seqStore_ = nullptr;
  bool*                   arcEnabled_ = nullptr;
  std::function<String()> stateJson_;
  std::function<String()> sequenceJson_;
  std::function<void(const String&, const String&)> wifiCreds_;
  std::function<void(uint8_t)> seqActivated_;
};
