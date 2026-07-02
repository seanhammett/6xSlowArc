# Slow Arc Controller — Firmware Report

One ESP32‑S3 box driving **six kinetic light artworks ("Slow Arcs")**. Each arc is a
dimmable halogen bulb‑pair plus a geared stepper. The firmware persists its settings,
runs standalone without a network, and exposes a web UI for commissioning and an
optional choreography mode.

---

## 1. Hardware

| Item | Detail |
|------|--------|
| MCU module | **ESP32‑S3‑WROOM‑2 (N32R8V)** — 32 MB octal (OPI) flash, 8 MB octal PSRAM, 1.8 V |
| Bulbs | 3 × **DFRobot DFR0971 (GP8403)** 2‑ch, 12‑bit, 0–10 V DAC → DIM14 dimmers. I²C addrs `0x58/0x59/0x5A` |
| Motors | 6 × **TMC2209** in STEP/DIR mode (current set by on‑board pot, microstep by MS pins) |
| Status LED | 1 × **WS2812** (on‑board RGB, GPIO 48) |
| Controls | Mode switch (GPIO 38), Sequence button (GPIO 39), both `INPUT_PULLUP`, active‑LOW |

**Pin map** ([pins.h](src/pins.h)): STEP `{4,5,6,7,15,16}`, DIR `{8,9,10,11,17,18}`,
EN `{1,2,3,12,13,14}` (active‑LOW, external 10k pull‑ups), I²C SDA `21` / SCL `47`.
Avoids GPIO 26–37 (octal flash/PSRAM) and the strapping pins.

**An arc = one DAC channel + one stepper.** DAC board `n` channels 0/1 map to arcs
`2n` / `2n+1`.

---

## 2. Build & flash

PlatformIO, `espressif32` / Arduino (core 2.0.17, IDF 4.4). Octal flash/PSRAM is
selected explicitly ([platformio.ini](platformio.ini)) — the stock `esp32-s3-devkitc-1`
preset is the N8 (quad) variant and bootloops on this module:

```ini
board_build.arduino.memory_type = opi_opi
board_build.flash_mode          = opi
board_upload.flash_size         = 32MB
```

Environments:

| Env | Purpose |
|-----|---------|
| `esp32-s3-devkitc-1` | **The firmware.** USB‑CDC console on the native USB port. |
| `test_dac` | Bench one GP8403: I²C scan + voltage sweep ([test_dac.cpp](src/test_dac.cpp)) |
| `test_stepper` | Bench one stepper via FastAccelStepper ([test_stepper.cpp](src/test_stepper.cpp)) |
| `test_arc` | Bench one DAC + one stepper together, MCPWM‑driven ([test_arc.cpp](src/test_arc.cpp)) |

```bash
pio run -e esp32-s3-devkitc-1 -t upload && pio device monitor
```

The test envs route `Serial` to UART0 (the CP2102 "UART" port) and compile only their
single source via `build_src_filter`; the app excludes the test files.

---

## 3. Software architecture

`main` owns the wiring between modules and the control loop; it is the only place that
sees every subsystem, so it assembles the web status snapshot and the status‑LED policy.

| Module | Responsibility |
|--------|----------------|
| [main.cpp](src/main.cpp) | Orchestration, control loop, state JSON, LED policy, task watchdog |
| [ChannelModel.h](src/ChannelModel.h) | The 12 set‑points (6 brightness + 6 speed) and the mode |
| [ConfigStore](src/ConfigStore.cpp) | NVS persistence (Preferences), debounced/coalesced writes |
| [BulbController](src/BulbController.cpp) | 3 GP8403 DACs over I²C; gamma curve, soft‑start ramp, inrush stagger |
| [MotorController](src/MotorController.cpp) | 6 steppers via **MCPWM**; software accel ramp; owns EN/safe‑state |
| [InputManager](src/InputManager.cpp) | Mode switch + sequence button (Bounce2 debounce) |
| [SequenceEngine](src/SequenceEngine.cpp) | Performance‑mode choreography (keyframe cues, eased) |
| [StatusLed](src/StatusLed.cpp) | Single WS2812, **bit‑bang** driver, state → colour/animation |
| [NetworkManager](src/NetworkManager.cpp) | Non‑blocking WiFi: STA with timed fallback to SoftAP |
| [OtaService](src/OtaService.cpp) | ArduinoOTA, with a safe‑state callback before flashing |
| [WebUi](src/WebUi.cpp) | AsyncWebServer; single‑page UI from PROGMEM; `/api/state`, `/api/set` |

The control loop is fully **non‑blocking**: WiFi, OTA, web, and NVS never stall it.

---

## 4. Behaviour

**Modes** (physical switch is authoritative):
- **Gallery** — every arc holds its static set‑point.
- **Performance** — the sequence engine drives the arcs; the button toggles run/stop.

**Persistence** — NVS is the single source of truth. The 12 set‑points + mode are
restored on boot and written back debounced (`NVS_SAVE_DEBOUNCE_MS = 1500 ms`) so slider
drags don't hammer flash. With WiFi down the box runs entirely from stored config.

**Bulbs** — 0..255 set‑point → gamma‑corrected (`γ = 2.2`) 0–10 V. Soft‑start ramps the
full range in `BULB_RAMP_MS = 350 ms`; a per‑channel **stagger** (`45 ms`) and large‑jump
re‑stagger keep twelve cold filaments from inrushing together on the shared rail.

**Motors** — 0..255 → `60..4000 Hz` step rate (0 = stopped), ramped in software at
`MOTOR_ACCEL_HZ_S = 1500 Hz/s` for soft start/stop.

**Status LED** (spec §7):

| State | Indication |
|-------|-----------|
| Boot / soft‑start | solid amber |
| Gallery healthy | slow green pulse |
| Performance running | slow blue pulse |
| WiFi connecting | amber pulse |
| OTA in progress | fast blue pulse |
| Fault | red blink, count = subsystem (2 = bulb/DAC, 3 = motor, 4 = supply/brownout) |

**Networking** — STA join is time‑bounded (`15 s`); on failure it raises SoftAP
`SlowArc-Setup` (`slowarc123`) so the UI is reachable on‑site, and keeps retrying STA in
the background (`30 s`). Credentials live in [wifi_config.h](src/wifi_config.h) (blank by
default → AP).

**Safety** — task watchdog (`8 s`, panic+reset). On any reset the EN pull‑ups hold all
drivers disabled; a brownout reset is detected and surfaced (likely bulb‑inrush rail sag).

---

## 5. Key design decision: MCPWM for the steppers

Each motor's speed **is** its STEP frequency, so six independent speeds need six
independent hardware timers. On the ESP32‑S3:

| Peripheral | Independent step trains | Verdict |
|-----------|------------------------|---------|
| RMT (FastAccelStepper) | 4 channels | ✗ caps at 4 motors |
| LEDC | 4 timers (channels pair up) | ✗ caps at 4 speeds |
| **MCPWM** | **2 units × 3 timers = 6** | ✓ exactly fits 6 arcs |

`MotorController` drives STEP from MCPWM: channel `i` → unit `i/3`, timer `i%3`,
generator A, as a 50 %‑duty square wave whose frequency is the speed. No RMT pressure,
no extra hardware, and the status LED is bit‑banged so it doesn't compete for RMT either.
Trade‑off vs FastAccelStepper: no hardware accel/positioning — but the arcs only rotate
continuously at a commissioned speed, so the software frequency ramp is sufficient.

The motor `begin()` enables each channel **independently**: a channel that fails to
initialise is skipped, the rest still run, and `Fault::Motor` is reported only as a
diagnostic (it no longer disables every motor).

---

## 6. Bring‑up history (resolved)

The board went through a multi‑stage boot‑loop diagnosis; all fixed:

1. **Wrong flash mode** — stock preset drove the octal flash as QIO → bootloader assert. Fixed with `opi_opi` / `flash_mode = opi` / 32 MB.
2. **WS2812 RMT hang** — Adafruit NeoPixel drives the LED over RMT with `RMT_WAIT_FOR_EVER`; once FastAccelStepper claimed the S3's RMT channels, `pixel.show()` blocked forever → task watchdog reboot. Fixed by replacing it with a bit‑bang WS2812 driver (no RMT).
3. **6 motors > 4 RMT channels** — FastAccelStepper can only drive 4 steppers on the S3. Fixed by moving to MCPWM (6 independent timers).
4. **All‑or‑nothing motor enable** — a single failed channel disabled all six. Fixed with per‑channel enable.

---

## 7. Known limits & TODO

- **Choreography is a placeholder** — [SequenceEngine.cpp](src/SequenceEngine.cpp) ships a demo "swell" cue table; replace with the artist's piece (`load()` accepts a new table).
- **No spare MCPWM timer** — all 6 are used; a 7th independent motor would need another approach.
- **WiFi credentials** are blank by default (ships in AP mode); set before deployment.
- **DAC false‑ACK** — the GP8403 library bit‑bangs I²C and can read a false ACK on a floating bus; the `test_dac` env scans the bus first to catch missing boards.
- **Supply** — the spec warns of inrush sag on the shared bulb rail; add bulk capacitance if brownout faults appear.
