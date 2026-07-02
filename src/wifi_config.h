#pragma once
//
// wifi_config.h — station credentials for the gallery's network.
//
// WiFi is OPTIONAL to operation: the box runs its stored NVS configuration with
// the network down. These credentials only matter for changing settings or
// performing OTA updates.
//
// Provisioning (spec §9.5): hardcoded here. If the join fails (or these are left
// blank) the firmware raises a SoftAP "SlowArc-Setup" so the web UI is still
// reachable on-site for commissioning. Replace before deployment.
//
// Treat this file as a secret — do not commit real credentials to a public repo.

static constexpr char WIFI_STA_SSID[] = "";   // <-- gallery SSID
static constexpr char WIFI_STA_PASS[] = "";   // <-- gallery password
