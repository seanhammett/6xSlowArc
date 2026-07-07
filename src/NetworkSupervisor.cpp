#include "NetworkSupervisor.h"

#include <WiFi.h>
#include <Preferences.h>

#include "config.h"
#include "wifi_config.h"

void NetworkSupervisor::begin() {
  // NVS credentials win; the compiled-in wifi_config.h pair only seeds a box
  // whose NVS has never been written.
  Preferences p;
  if (p.begin("net", /*readOnly=*/true)) {
    staSsid_ = p.getString("ssid", WIFI_STA_SSID);
    staPass_ = p.getString("pass", WIFI_STA_PASS);
    p.end();
  } else {
    staSsid_ = WIFI_STA_SSID;
    staPass_ = WIFI_STA_PASS;
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);   // we manage reconnection ourselves, bounded

  // Bring up ONLY the interface we are going to use. Cycling the radio through
  // STA and straight into AP within a few ms races the binary WiFi stack's
  // power-management task during first-boot PHY calibration ("Cache disabled
  // but cached memory region accessed" panic in pm_disconnected_wake/ppTask).
  if (staSsid_.length()) {
    startStationAttempt();
  } else if (WIFI_AP_FALLBACK) {
    startAccessPoint();
  }
}

void NetworkSupervisor::setCredentials(const String& ssid, const String& pass) {
  Preferences p;
  p.begin("net", /*readOnly=*/false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();

  staSsid_ = ssid;
  staPass_ = pass;
  if (staSsid_.length()) startStationAttempt();   // AP (if up) stays alive: AP+STA
}

void NetworkSupervisor::startStationAttempt() {
  // Keep an active AP alive during the attempt so the commissioning client is
  // not dropped if the join fails.
  WiFi.mode(apUp_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(staSsid_.c_str(), staPass_.c_str());
  WiFi.setSleep(false);           // low latency for the web UI
  status_ = Status::Connecting;
  attemptStartedMs_ = millis();
}

void NetworkSupervisor::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  apUp_ = true;
  // Captive portal: answer every DNS query with our own address so joining the
  // AP pops the setup page.
  dns_.start(53, "*", WiFi.softAPIP());
  status_ = Status::AccessPoint;
  lastRetryMs_ = millis();
}

void NetworkSupervisor::stopAccessPoint() {
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);            // the station link (if any) is untouched
  apUp_ = false;
}

void NetworkSupervisor::tick() {
  if (apUp_) dns_.processNextRequest();

  switch (status_) {
    case Status::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        status_ = Status::StationConnected;
        // Portal-started join: let the commissioning client read the new
        // address before the AP goes away.
        if (apUp_) apCloseAtMs_ = millis() + WIFI_AP_LINGER_MS;
      } else if (millis() - attemptStartedMs_ > WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();                      // stop the join; AP unaffected
        lastRetryMs_ = millis();
        if (apUp_)                 status_ = Status::AccessPoint;
        else if (WIFI_AP_FALLBACK) startAccessPoint();
        else                       status_ = Status::Offline;
      }
      break;

    case Status::StationConnected:
      if (WiFi.status() != WL_CONNECTED) {
        // Link dropped — bounce back into a bounded reconnect.
        status_ = Status::Offline;
        lastRetryMs_ = millis();
      } else if (apUp_ && (int32_t)(millis() - apCloseAtMs_) > 0) {
        stopAccessPoint();                      // linger over; STA-only now
      }
      break;

    case Status::Offline:
    case Status::AccessPoint:
      // Periodically re-attempt the station join if we have creds. A successful
      // join supersedes the AP (after the linger).
      if (staSsid_.length() && millis() - lastRetryMs_ > WIFI_RETRY_INTERVAL_MS) {
        lastRetryMs_ = millis();
        startStationAttempt();
      }
      break;
  }
}

String NetworkSupervisor::ipAddress() const {
  if (status_ == Status::StationConnected) return WiFi.localIP().toString();
  if (apUp_) return WiFi.softAPIP().toString();
  return String();
}

String NetworkSupervisor::ssid() const {
  switch (status_) {
    case Status::StationConnected: return WiFi.SSID();
    case Status::Connecting:       return staSsid_;
    default:                       return apUp_ ? String(WIFI_AP_SSID) : String();
  }
}
