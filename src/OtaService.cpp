#include "OtaService.h"

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "config.h"

void OtaService::begin(std::function<void()> onStart) {
  onStart_ = onStart;
}

void OtaService::handle() {
  if (!enabled_) return;

  if (!started_) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.onStart([this]() {
      inProgress_ = true;
      if (onStart_) onStart_();      // drop motors into the safe state first
    });
    ArduinoOTA.onEnd([this]() { inProgress_ = false; });
    ArduinoOTA.onError([this](ota_error_t) { inProgress_ = false; });
    ArduinoOTA.begin();
    started_ = true;
  }

  ArduinoOTA.handle();
}
