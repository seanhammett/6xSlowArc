#pragma once
//
// NetworkManager — non-blocking WiFi for an installation where the network is
// OPTIONAL (spec §8). The control loop must NEVER block on the radio.
//
// Behaviour:
//   * If station credentials are present, attempt to join. The attempt is
//     time-bounded; on success we're a client on the gallery LAN.
//   * If the join times out (or no creds, or the link later drops) we retry on a
//     bounded interval and — when WIFI_AP_FALLBACK is set — raise a SoftAP so the
//     web UI stays reachable on-site for commissioning.
//   * Everything is polled from tick(); nothing here ever spins waiting.
//

#include <stdint.h>
#include <Arduino.h>   // for String

class NetworkManager {
 public:
  enum class Status : uint8_t { Offline, Connecting, StationConnected, AccessPoint };

  void begin();
  void tick();

  Status status() const { return status_; }
  bool   connecting() const { return status_ == Status::Connecting; }
  bool   online() const {
    return status_ == Status::StationConnected || status_ == Status::AccessPoint;
  }

  // Human-readable address/SSID for the status page. Returns "" when offline.
  String ipAddress() const;
  String ssid() const;

 private:
  void startStationAttempt();
  void startAccessPoint();

  Status   status_ = Status::Offline;
  uint32_t attemptStartedMs_ = 0;
  uint32_t lastRetryMs_ = 0;
  bool     haveStaCreds_ = false;
};
