#pragma once
//
// wifi_config.h — OPTIONAL compiled-in seed for the station credentials.
//
// The real credentials live in NVS, set on-site from the web UI's WiFi card
// (join the "SlowArc-Setup" AP — the captive portal pops the page — scan, pick
// the network, enter the password). Anything stored in NVS overrides this file,
// so these can normally stay blank; fill them in only to pre-provision a box
// whose NVS has never been written.
//
// WiFi is OPTIONAL to operation: the box runs its stored NVS configuration with
// the network down. It only matters for changing settings or OTA updates.
//
// Treat this file as a secret — do not commit real credentials to a public repo.

static constexpr char WIFI_STA_SSID[] = "";   // <-- gallery SSID
static constexpr char WIFI_STA_PASS[] = "";   // <-- gallery password
