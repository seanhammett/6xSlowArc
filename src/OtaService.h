#pragma once
//
// OtaService — ArduinoOTA wrapper. WiFi is the primary update path; USB is the
// fallback. Only meaningful while the network is up; harmless otherwise.
//
// The service reports when a transfer is in progress so the main loop can show
// the OTA LED pattern and suppress motion during the flash.
//

#include <stdint.h>
#include <functional>

class OtaService {
 public:
  // onStart is invoked when a transfer begins — use it to put the rig in a safe
  // state (motors disabled) before the flash write.
  void begin(std::function<void()> onStart = nullptr);

  // Only call once the network is online. Cheap no-op before that.
  void setEnabled(bool enabled) { enabled_ = enabled; }

  void handle();

  bool inProgress() const { return inProgress_; }

 private:
  bool enabled_ = false;
  bool started_ = false;
  bool inProgress_ = false;
  std::function<void()> onStart_;
};
