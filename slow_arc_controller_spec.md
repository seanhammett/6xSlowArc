# Slow Arc Controller — Firmware Specification

A single control box driving six kinetic light artworks ("Slow Arcs"). Each Slow Arc consists of a pair of 24V 70W halogen bulbs (dimmed) and one geared stepper motor (speed-controlled). The box has a small set of physical controls (power, mode switch, sequence button, status LED) and hosts a WiFi status/control web page that is the means of setting per-channel brightness and speed. This document is the build spec for the firmware.

Target platform: **ESP32-S3** (Arduino framework / PlatformIO). WiFi + OTA enabled.

---

## 1. System at a glance

- 6 channels, each = { 1 dimmable halogen bulb-pair, 1 stepper motor }.
- Brightness and motor speed are each set per-channel (6 + 6 = 12 control values).
- Control is via the WiFi web UI (or hardcoded firmware defaults). There are **no physical slider/pot controls** — the 12 values are set once at commissioning and persisted.
- Two operating modes: **Gallery** (set-and-hold) and **Performance** (sequenced).
- Set-and-hold usage: values are configured at commissioning and then left. Changes are rare and human-speed.
- Long-running gallery duty: design for continuous operation, hot loads, and graceful recovery from power loss.

---

## 2. Hardware context (what the firmware talks to)

| Function | Device | Interface |
|---|---|---|
| MCU | ESP32-S3 DevKit | — |
| Bulb dimming control | 3 × DFRobot DFR0971 (2-ch, 12-bit, 0–10V I²C DAC, GP8403) → 6 × Abeltronics DIM14 | I²C, addrs 0x58–0x5F (DIP) |
| Stepper motion | 6 × TMC2209 (name-brand, socketed/replaceable) | STEP/DIR/EN GPIO |
| Status indication | 1 × WS2812 RGB LED | 1 GPIO |
| Mode switch | Gallery / Performance | 1 GPIO |
| Sequence button | Start / Stop | 1 GPIO |
| Power (bulbs+motors) | 24V 1000W CV supply (spec HEP-1000-24, fanless/enclosed) | — |
| Logic supply | 24V→5V/3V3, **own bulk cap or separate supply** (see §8) | — |

Notes for the firmware author:
- The DAC boards are **DFRobot DFR0971** (GP8403 chip), 2-channel, 12-bit, 0–10V output. Use **3 boards** for 6 total channels. I²C addresses are set via onboard DIP switch in the range 0x58–0x5F; assign three distinct addresses (e.g. 0x58, 0x59, 0x5A). Channel 0 of each board maps to one Slow Arc; Channel 1 to the next. Use DFRobot’s GP8403 Arduino library. The DIM14 accepts 0V = off, 10V = full; the firmware writes a 12-bit value (0x000–0xFFF) per channel — it never PWMs the bulbs directly.
- TMC2209s are driven in **STEP/DIR mode** (not UART). Use the FastAccelStepper library; current is set by the driver's onboard potentiometer in hardware.
- The motor is a geared NEMA 11 (11HS12-0674D-PG27, 26.85:1, 0.67A/phase). Worst-case coil power ≈5W; trivial electrically. Mechanical limits (gearbox 3Nm, radial 25N) are a hardware concern, not firmware.

---

## 3. I/O map (to be finalised against the actual board)

The author should produce a concrete `pins.h`. Reserve GPIO for:

- 6 × STEP (output; add 10kΩ pull-down externally to suppress boot glitches)
- 6 × DIR (output)
- 6 × EN (output; default to **disabled** state at boot — drivers should be off until firmware is ready; external pull-up recommended)
- I²C: SDA, SCL (shared by the DAC[s])
- 1 × WS2812 data
- 1 × Mode switch (INPUT_PULLUP)
- 1 × Sequence button (INPUT_PULLUP, debounced)

Avoid ESP32-S3 strapping pins (GPIO 0, 45, 46) for outputs that must be quiet at boot. Input-only constraints do not apply to S3 GPIO the way they do on classic ESP32, but verify each chosen pin.

---

## 4. Firmware architecture

Suggested module breakdown:

- **`ChannelModel`** — holds, per channel (0–5): `brightnessTarget`, `brightnessCurrent`, `speedTarget`, `speedCurrent`. Values originate from the web UI / NVS defaults.
- **`BulbController`** — owns the DAC(s). Takes per-channel brightness set-points from `ChannelModel`, applies gamma correction, writes 0–10V per channel. Implements soft-start ramping.
- **`MotorController`** — owns 6 FastAccelStepper instances. Takes per-channel speed set-points from `ChannelModel`, maps to step rate, commands ramped speed changes. Owns EN lines and the safe-shutdown path.
- **`InputManager`** — debounces Mode switch and Sequence button (Bounce2).
- **`SequenceEngine`** — Performance-mode choreography (see §5). No-op in Gallery mode.
- **`StatusLed`** — owns the WS2812; maps system state to colour/animation (see §7).
- **`ConfigStore`** — persists the 12 set-points (and mode) to NVS; restores on boot.
- **`WebServer`** — hosts the status/control page over WiFi; exposes read/write of the 12 control values and system state (see §6). Writes go through `ConfigStore`.
- **`OtaService`** — ArduinoOTA; WiFi is primary update path, USB is fallback.
- Main loop feeds a **task watchdog**; on hang, reset must leave motors disabled (EN safe state) — bulbs will simply go dark as the DAC loses drive.

---

## 5. Operating modes

**Gallery (default):** Each channel holds the brightness and speed configured at commissioning (via web UI, persisted to NVS). No automated changes. "Set and forget." This is the primary, most-used mode.

**Performance:** The `SequenceEngine` drives brightness/speed over time as a choreographed sequence. The Sequence Start/Stop button starts/stops playback. The exact sequence content is **TBD** — implement the engine as a timeline/step interpreter with a placeholder sequence and a clear interface for defining cues, so the artist's choreography can be added later. Define what "Stop" does: hold current values, or return to a defined rest state (assume **hold current values** unless told otherwise; make it a one-line config).

Mode switch selects between the two. Switching to Gallery should cleanly hand control back to the static set-points.

*Revision (ceiling install):* the front panel may be out of reach, so the web UI can also set the mode and play/stop the sequence. Last change wins between the switch (on its flip) and the web; the saved mode is restored at boot, stopped. The status LED overlays a white blip while the switch position disagrees with the actual mode.

---

## 6. Control source & persistence

There is a single source of control: the **web UI** (plus hardcoded firmware defaults for first boot). No precedence logic is needed.

- The 12 set-points (6 brightness, 6 speed) and the current mode are held in `ChannelModel` and persisted to **NVS** via `ConfigStore`.
- On boot, restore the last-saved values from NVS; if none exist, fall back to compiled-in defaults.
- The web page exposes, at minimum: per-channel brightness and speed (read + write), current mode, sequence running state, and overall health/status.
- Writes from the web UI update `ChannelModel` and persist to NVS (debounced — don't hammer NVS on every slider drag; write on release / after a short settle).
- Because values are set once at commissioning and rarely changed, the web UI is a configuration surface, not a real-time control loop. It must not be required for normal running (see §8, WiFi-optional).

---

## 7. Status LED conventions (WS2812)

| State | Indication |
|---|---|
| Boot / soft-start | Solid amber |
| Gallery, healthy | Slow green pulse |
| Performance, sequence running | Slow blue pulse |
| WiFi connecting | Amber pulse |
| OTA in progress | Fast blue pulse |
| Fault | Red blink; encode subsystem by blink count (e.g. 2 = bulb/DAC, 3 = motor, 4 = supply/brownout) |

The LED is the primary on-box diagnostic (no screen). Keep meanings documented on the box.

---

## 8. Reliability requirements (these are firmware-affecting, not optional)

**Halogen inrush is the dominant electrical risk.** Twelve cold filaments on one 1000W rail can draw many times the 840W steady-state at switch-on, risking supply over-current/hiccup or a rail sag that browns out the logic and resets the MCU. Firmware MUST:

1. **Soft-start** — ramp each channel's 0–10V control from 0 to target over ~100–500 ms rather than stepping. Required at power-on and at any large brightness increase.
2. **Stagger** — offset the six channels' turn-on by a few tens of ms so they don't all inrush simultaneously.

Other requirements:

- **Brightness output curve:** apply gamma (squared/cubed) so configured brightness maps to perceptually-linear output on the filament. Quantize to ≤256 levels; full DAC resolution is unnecessary.
- **Motor commands ramped:** never command instantaneous speed jumps; use FastAccelStepper accel/decel.
- **Safe shutdown:** watchdog reset (or any fault) must drive EN lines to the disabled state. EN defaults disabled at boot.
- **State recovery:** on power-up, restore the 12 set-points and mode from NVS and ramp to those values (soft-start applies to brightness). NVS is the single source of persisted state.
- **Logic supply isolation:** the firmware can't fix a hardware brownout, but it should detect ESP32 brownout/reset and report it (status LED fault code). Hardware side: logic supply needs its own bulk capacitance or a separate supply so bulb inrush can't reset the brain. Flag this to the hardware build.
- **WiFi optional to operation:** once commissioned, the box runs its stored configuration with WiFi down — the web/WiFi layer is only needed to *change* settings or update firmware, never for normal running. Bound WiFi reconnect attempts; never block the control loop on the network.

---

## 9. Open items for the author to confirm

1. Performance-mode sequence content (placeholder + clean cue interface until artist supplies it).
2. "Stop" behaviour in Performance mode (assume hold-current unless specified).
3. Final GPIO assignment in `pins.h` against the actual S3 board variant.
4. Exact halogen bulb part (affects only documentation of inrush margin; firmware soft-start covers it regardless).
5. WiFi provisioning method for install (hardcoded, captive portal, or config file).

---

## 10. Deliverables expected from the coding agent

- PlatformIO project targeting ESP32-S3, Arduino framework.
- Libraries: FastAccelStepper, DFRobot_GP8403 (DAC), Adafruit_NeoPixel (or FastLED), Bounce2, an async web server (e.g. ESPAsyncWebServer), ArduinoOTA, Preferences (NVS).
- Clean module separation per §4.
- `pins.h` with documented assignments.
- Soft-start + stagger implemented and tunable via constants.
- Web control/status page served from the device; writes persisted to NVS.
- Graceful behaviour with WiFi unavailable (runs stored config).
- Inline documentation of the persistence model and the status-LED codes.
