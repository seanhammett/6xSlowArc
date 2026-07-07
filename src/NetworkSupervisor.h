#pragma once
//
// NetworkSupervisor — non-blocking WiFi for an installation where the network is
// OPTIONAL (spec §8). The control loop must NEVER block on the radio.
//
// Provisioning: station credentials live in NVS (namespace "net"); the pair in
// wifi_config.h is only a compiled-in seed for a fresh box. With no (or bad)
// credentials a SoftAP + captive portal comes up, and the web UI's WiFi card
// (scan / pick / password) calls setCredentials() to join the real network —
// so deployment needs no recompile.
//
// Behaviour:
//   * Boot: try the stored station credentials (time-bounded); with none, raise
//     the SoftAP straight away.
//   * Join attempts made while the AP is up run in AP+STA so the commissioning
//     client is never dropped by a failed attempt; after a successful join the
//     AP lingers WIFI_AP_LINGER_MS so that client can read the new address,
//     then closes.
//   * While the AP is up, a DNS catch-all resolves every name to the box so
//     joining the AP pops the setup page (captive portal).
//   * Everything is polled from tick(); nothing here ever spins waiting.
//

#include <stdint.h>
#include <Arduino.h>
#include <DNSServer.h>

class NetworkSupervisor {
 public:
  enum class Status : uint8_t { Offline, Connecting, StationConnected, AccessPoint };

  void begin();
  void tick();

  // Persist new station credentials to NVS and try them immediately (web UI).
  // An empty ssid clears the stored pair (the box then lives on the SoftAP).
  void setCredentials(const String& ssid, const String& pass);

  Status status() const { return status_; }
  bool   connecting() const { return status_ == Status::Connecting; }
  bool   online() const { return status_ == Status::StationConnected || apUp_; }

  // Human-readable ssid/address for the status page. Empty when offline.
  String ipAddress() const;
  String ssid() const;

 private:
  void startStationAttempt();
  void startAccessPoint();
  void stopAccessPoint();

  Status    status_ = Status::Offline;
  String    staSsid_, staPass_;
  DNSServer dns_;
  bool      apUp_ = false;
  uint32_t  apCloseAtMs_ = 0;         // when to close a lingering AP
  uint32_t  attemptStartedMs_ = 0;
  uint32_t  lastRetryMs_ = 0;
};
