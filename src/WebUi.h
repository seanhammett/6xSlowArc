#pragma once
//
// WebUi — the WiFi status/control page (spec §6).
//
// Hosts an async HTTP server exposing:
//   GET  /                 the control/status page (sliders + live status)
//   GET  /api/state        JSON snapshot of the 12 set-points + system state
//   POST /api/set          write one channel's brightness and/or speed
//   GET  /api/sequence     the choreography table (for the timeline view)
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

class WebUi {
 public:
  // stateJson must return a complete JSON object describing live system state;
  // sequenceJson the choreography table. onWifiCredentials receives (ssid, pass)
  // from the WiFi setup card.
  void begin(ChannelModel* model, ConfigStore* store,
             std::function<String()> stateJson,
             std::function<String()> sequenceJson,
             std::function<void(const String&, const String&)> onWifiCredentials);

 private:
  ChannelModel*           model_ = nullptr;
  ConfigStore*            store_ = nullptr;
  std::function<String()> stateJson_;
  std::function<String()> sequenceJson_;
  std::function<void(const String&, const String&)> wifiCreds_;
};
