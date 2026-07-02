#include "NetworkManager.h"

#include <WiFi.h>

#include "config.h"
#include "wifi_config.h"

void NetworkManager::begin() {
  haveStaCreds_ = (strlen(WIFI_STA_SSID) > 0);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);   // we manage reconnection ourselves, bounded
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (haveStaCreds_) {
    startStationAttempt();
  } else if (WIFI_AP_FALLBACK) {
    startAccessPoint();
  }
}

void NetworkManager::startStationAttempt() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
  status_ = Status::Connecting;
  attemptStartedMs_ = millis();
}

void NetworkManager::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  status_ = Status::AccessPoint;
  lastRetryMs_ = millis();
}

void NetworkManager::tick() {
  switch (status_) {
    case Status::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        status_ = Status::StationConnected;
      } else if (millis() - attemptStartedMs_ > WIFI_CONNECT_TIMEOUT_MS) {
        // Give up this attempt; fall back to AP (if enabled) and keep retrying
        // the station in the background.
        if (WIFI_AP_FALLBACK) {
          startAccessPoint();
        } else {
          WiFi.disconnect(true);
          status_ = Status::Offline;
          lastRetryMs_ = millis();
        }
      }
      break;

    case Status::StationConnected:
      if (WiFi.status() != WL_CONNECTED) {
        // Link dropped — bounce back into a bounded reconnect.
        status_ = Status::Offline;
        lastRetryMs_ = millis();
      }
      break;

    case Status::Offline:
    case Status::AccessPoint:
      // Periodically re-attempt the station join if we have creds. A successful
      // join supersedes the AP.
      if (haveStaCreds_ && millis() - lastRetryMs_ > WIFI_RETRY_INTERVAL_MS) {
        lastRetryMs_ = millis();
        startStationAttempt();
      }
      break;
  }
}

String NetworkManager::ipAddress() const {
  switch (status_) {
    case Status::StationConnected: return WiFi.localIP().toString();
    case Status::AccessPoint:      return WiFi.softAPIP().toString();
    default:                       return String();
  }
}

String NetworkManager::ssid() const {
  switch (status_) {
    case Status::StationConnected: return WiFi.SSID();
    case Status::AccessPoint:      return String(WIFI_AP_SSID);
    default:                       return String();
  }
}
