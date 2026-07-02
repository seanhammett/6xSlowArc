#pragma once
//
// WebUi — the WiFi status/control page (spec §6).
//
// Hosts an async HTTP server exposing:
//   GET  /                 the control/status page (sliders + live status)
//   GET  /api/state        JSON snapshot of the 12 set-points + system state
//   POST /api/set          write one channel's brightness and/or speed
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

class WebUi {
 public:
  // stateJson must return a complete JSON object describing live system state.
  void begin(ChannelModel* model, ConfigStore* store,
             std::function<String()> stateJson);

 private:
  ChannelModel*           model_ = nullptr;
  ConfigStore*            store_ = nullptr;
  std::function<String()> stateJson_;
};
