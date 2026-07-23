# Slow Arc Controller — Firmware Report

One ESP32‑S3 box driving **six kinetic light artworks ("Slow Arcs")**. Each arc is a
dimmable halogen bulb‑pair plus a geared stepper. The firmware persists its settings,
runs standalone without a network, and exposes a web UI for commissioning, per‑arc
control, and artist‑editable choreography.

---

## 1. Hardware

| Item | Detail |
|------|--------|
| MCU module | **ESP32‑S3‑WROOM‑2 (N32R8V)** — 32 MB octal (OPI) flash, 8 MB octal PSRAM, 1.8 V |
| Bulbs | 3 × **DFRobot DFR0971 (GP8403)** 2‑ch, 12‑bit, 0–10 V DAC → DIM14 dimmers. I²C addrs `0x58/0x59/0x5A` |
| Motors | 6 × **TMC2209** in STEP/DIR mode (current set by on‑board pot, microstep by MS pins) |
| Status LED | 2 × **WS2812** in parallel — on‑board RGB (GPIO 38) + off‑board panel mirror (GPIO 40) |
| Controls | Mode switch (GPIO 48), Sequence button (GPIO 39), both `INPUT_PULLUP`, active‑LOW |

**Pin map** ([pins.h](src/pins.h)): STEP `{4,5,6,7,15,16}`, DIR `{8,9,10,11,17,18}`,
EN `{1,2,3,12,13,14}` (active‑LOW, external 10k pull‑ups), I²C SDA `21` / SCL `47`.
Avoids GPIO 26–37 (octal flash/PSRAM) and the strapping pins. Both LED pins must be
≥ 32 — the bit‑bang driver writes them together through the `GPIO_OUT1` register bank,
so the two pixels always show the same colour from one waveform.

**An arc = one DAC channel + one stepper.** DAC board `n` channels 0/1 map to arcs
`2n` / `2n+1`.

---

## 2. Build & flash

PlatformIO, `espressif32` / Arduino. The platform is intentionally **unpinned**, so it
resolves to the current Arduino 3.x / IDF 5.x core; the code carries the compatibility
shims that needs (e.g. the `esp_task_wdt_config_t` init path in
[main.cpp](src/main.cpp)). Octal flash/PSRAM is selected explicitly
([platformio.ini](platformio.ini)) — the stock `esp32-s3-devkitc-1` preset is the N8
(quad) variant and bootloops on this module:

```ini
board_build.arduino.memory_type = opi_opi
board_build.flash_mode          = opi
board_upload.flash_size         = 32MB
```

Environments:

| Env | Purpose |
|-----|---------|
| `esp32-s3-devkitc-1` | **The firmware.** |
| `test_dac` | Bench one GP8403: I²C scan + voltage sweep ([test_dac.cpp](src/test_dac.cpp)) |
| `test_stepper` | Bench one stepper via FastAccelStepper, channel picked by `TEST_STEP_CH` ([test_stepper.cpp](src/test_stepper.cpp)) |
| `test_arc` | Concurrency test: all 6 MCPWM motors + the DACs at once, one web slider each ([test_arc.cpp](src/test_arc.cpp)) |

```bash
pio run -e esp32-s3-devkitc-1 -t upload && pio device monitor
```

**Every** env puts `Serial` on UART0 (the CP2102 "UART" port) — `ARDUINO_USB_MODE=1`,
`ARDUINO_USB_CDC_ON_BOOT=0` — so one cable and one port works for all of them. Set
`ARDUINO_USB_CDC_ON_BOOT=1` for the native‑USB CDC console instead. The test envs
compile only their single source via `build_src_filter`; the app excludes the test
files, and `test_arc` additionally pulls in the production `StatusLed` + `InputManager`.

`test_arc` joins WiFi using the gitignored [secrets.h](src/secrets.h.example) — copy
`secrets.h.example` to `secrets.h` and fill it in; a fresh clone without it still
compiles (guarded by `__has_include`).

---

## 3. Software architecture

`main` owns the wiring between modules and the control loop; it is the only place that
sees every subsystem, so it assembles the web status snapshot and the status‑LED policy.

| Module | Responsibility |
|--------|----------------|
| [main.cpp](src/main.cpp) | Orchestration, control loop, state JSON, per‑arc kill switches, LED policy, task watchdog |
| [ChannelModel.h](src/ChannelModel.h) | The 12 set‑points (6 brightness + 6 speed) and the mode |
| [SystemState.h](src/SystemState.h) | Dependency‑free `Fault` / `LedState` enums shared across modules |
| [ConfigStore](src/ConfigStore.cpp) | NVS persistence of the set‑points (versioned blob), debounced/coalesced writes |
| [BulbController](src/BulbController.cpp) | 3 GP8403 DACs over I²C; linear map, soft‑start ramp, inrush stagger |
| [MotorController](src/MotorController.cpp) | 6 steppers via **MCPWM**; software accel ramp; owns EN/safe‑state |
| [InputManager](src/InputManager.cpp) | Mode switch + sequence button (Bounce2, 25 ms) |
| [SequenceEngine](src/SequenceEngine.cpp) | Performance‑mode choreography: expands steps+ramp into an interpolated cue table |
| [SequenceStore](src/SequenceStore.cpp) | 8 sequence slots in NVS + the active slot; seeds the built‑in piece on first boot |
| [StatusLed](src/StatusLed.cpp) | Two WS2812s, **bit‑bang** driver, state → colour/animation |
| [NetworkSupervisor](src/NetworkSupervisor.cpp) | Non‑blocking WiFi: NVS credentials, STA/SoftAP, captive portal |
| [OtaService](src/OtaService.cpp) | ArduinoOTA, with a safe‑state callback before flashing |
| [WebUi](src/WebUi.cpp) | AsyncWebServer; single‑page UI from PROGMEM, Control + Sequences tabs |

The control loop is fully **non‑blocking**: WiFi, OTA, web, and NVS never stall it.

---

## 4. Behaviour

**Modes** (physical switch is authoritative — closed = Performance, open = Gallery):
- **Gallery** — every arc holds its static set‑point.
- **Performance** — the sequence engine drives the arcs; the button toggles run/stop.

**Persistence** — NVS is the single source of truth, in two namespaces: `slowarc` holds
the 12 set‑points + mode as one versioned packed blob ([ConfigStore.cpp](src/ConfigStore.cpp));
`seqs` holds the sequence slots. Both are restored on boot; set‑point writes are
debounced (`NVS_SAVE_DEBOUNCE_MS = 1500 ms`) so slider drags don't hammer flash. With
WiFi down the box runs entirely from stored config.

**Bulbs** — 0..255 set‑point maps **linearly** to 0–10 V. There is deliberately *no*
firmware gamma curve: the DIM14 dimmer applies its own perceptual correction downstream,
and doing it in both places compounds and crushes the low end. Soft‑start ramps the full
range in `BULB_RAMP_MS = 350 ms`; a per‑channel **stagger** (`BULB_STAGGER_MS = 45 ms`)
and a re‑stagger on large jumps (`BULB_LARGE_STEP = 24`) keep twelve cold filaments from
inrushing together on the shared 1000 W rail.

**Motors** — 0..255 → `60..4000 Hz` step rate (0 = stopped), ramped in software at
`MOTOR_ACCEL_HZ_S = 1500 Hz/s` for soft start/stop.

**Per‑arc kill switches** — the web UI can force any arc dark and stopped regardless of
mode. Runtime only: never persisted, boots all‑on, and the set‑points are left untouched.
The mask is applied to copies of the effective frame, so a held sequence frame survives
an off/on cycle.

**Status LED** (spec §7) — both pixels show the same thing:

| State | Indication |
|-------|-----------|
| Boot / soft‑start | solid amber |
| Gallery healthy | slow green pulse |
| Performance running | slow blue pulse |
| WiFi connecting | amber pulse |
| OTA in progress | fast blue pulse |
| Fault | red blink, count = subsystem (2 = bulb/DAC, 3 = motor, 4 = supply/brownout) |

**Safety** — task watchdog (`8 s`, panic+reset), unsubscribed during OTA and re‑armed if
the OTA aborts. On any reset the EN pull‑ups hold all drivers disabled; a brownout reset
is detected and surfaced as `Fault::Supply` for 15 s (likely bulb‑inrush rail sag).

---

## 5. Choreography

A sequence is an **artist‑editable list of steps**, authored on the web UI's Sequences
tab and stored in NVS ([SequenceEngine.h](src/SequenceEngine.h)):

- A `SeqStep` is "at time T, these arcs are on" — a timestamp plus a 6‑bit mask. Bulb and
  motor always move together, so an arc is one bit.
- A `SeqDef` is a name, up to `SEQ_MAX_STEPS = 32` steps, and **one shared ramp** used for
  every rise and fall.
- `apply()` expands that into the internal cue table: each step becomes a hold cue at
  `T − ramp` plus a target cue at `T`, and the loop closes at `lastT + ramp` back to the
  t=0 state. Between cues the engine interpolates linearly, so the piece eases rather
  than snaps. The timeline loops.

Cue values are **scales of the stored set‑points** (255 = the set‑point, 0 = off), so the
choreography decides only *when* each arc moves; how bright/fast it goes stays with the
per‑channel commissioning, and re‑commissioning retunes the piece automatically.

`SequenceStore` keeps `SEQ_SLOTS = 8` slots plus the active index. On first boot it seeds
slot 0 with the built‑in piece **"Original"** (4 s ramp, 56 s holds: the arcs wake one at
a time, all six hold, then fall away in the same order; the loop closes at 668 s). Saving
by name overwrites the matching slot or takes the first free one. Selecting or re‑saving
the active sequence hands it to the engine — live if stopped, queued for the next button
push if running.

Stop behaviour is set by `SEQUENCE_STOP_HOLD` (config.h): hold the last produced frame
(default) or hand back to the static gallery set‑points.

---

## 6. Web UI & networking

Single PROGMEM page, two tabs:

- **Control** — per‑arc brightness/speed sliders, per‑arc on/off buttons, live status
  (mode, running, WiFi, IP, fault, uptime), a timeline view of the running sequence, and
  the WiFi provisioning card.
- **Sequences** — list/select stored sequences, preview, and create/save a new one.

| Route | Purpose |
|-------|---------|
| `GET /` | the page |
| `GET /api/state` | JSON snapshot: set‑points, per‑arc on/off, mode, running, `seq_t`, wifi/ssid/ip, fault, uptime |
| `POST /api/set` | write one channel's brightness and/or speed |
| `POST /api/arc` | per‑arc kill switch (runtime only, not saved) |
| `GET /api/sequence` | the engine's current cue table (timeline view) |
| `GET /api/seqs` | list stored sequences + the active slot |
| `GET /api/seq?i=N` | one stored sequence definition |
| `POST /api/seq/select` | make slot N active |
| `POST /api/seq/save` | save a sequence (name, ramp, steps) |
| `GET /api/scan` | async WiFi scan (202 while running, 200 + list when done) |
| `POST /api/wifi` | store station credentials |

Unknown paths redirect to `/` while the SoftAP is up (captive portal).

**Provisioning** — station credentials live in NVS (namespace `net`).
[wifi_config.h](src/wifi_config.h) is only a compiled‑in *seed* for a box whose NVS has
never been written; anything stored overrides it, so it normally stays blank and
deployment needs no recompile. With no or bad credentials the box raises SoftAP
`SlowArc-Setup` / `slowarc123` with a DNS catch‑all, so joining it pops the setup page —
scan, pick the network, enter the password.

STA joins are time‑bounded (`15 s`) and retried in the background (`30 s`). A join
attempted while the AP is up runs in **AP+STA**, so a failed attempt never drops the
commissioning client; after a successful join the AP lingers `WIFI_AP_LINGER_MS = 60 s`
so that client can read the new address, then closes.

---

## 7. Key design decision: MCPWM for the steppers

Each motor's speed **is** its STEP frequency, so six independent speeds need six
independent hardware timers. On the ESP32‑S3:

| Peripheral | Independent step trains | Verdict |
|-----------|------------------------|---------|
| RMT (FastAccelStepper) | 4 channels | ✗ caps at 4 motors |
| LEDC | 4 timers (channels pair up) | ✗ caps at 4 speeds |
| **MCPWM** | **2 units × 3 timers = 6** | ✓ exactly fits 6 arcs |

`MotorController` drives STEP from MCPWM: channel `i` → unit `i/3`, timer `i%3`,
generator A, as a 50 %‑duty square wave whose frequency is the speed. No RMT pressure,
no extra hardware, and the status LEDs are bit‑banged so they don't compete for RMT
either. Trade‑off vs FastAccelStepper: no hardware accel/positioning — but the arcs only
rotate continuously at a commissioned speed, so the software frequency ramp is sufficient.

The motor `begin()` enables each channel **independently**: a channel that fails to
initialise is skipped, the rest still run, and `Fault::Motor` is reported only as a
diagnostic (it no longer disables every motor).

---

## 8. Bring‑up history (resolved)

The board went through a multi‑stage boot‑loop diagnosis; all fixed:

1. **Wrong flash mode** — stock preset drove the octal flash as QIO → bootloader assert. Fixed with `opi_opi` / `flash_mode = opi` / 32 MB.
2. **WS2812 RMT hang** — Adafruit NeoPixel drives the LED over RMT with `RMT_WAIT_FOR_EVER`; once FastAccelStepper claimed the S3's RMT channels, `pixel.show()` blocked forever → task watchdog reboot. Fixed by replacing it with a bit‑bang WS2812 driver (no RMT).
3. **6 motors > 4 RMT channels** — FastAccelStepper can only drive 4 steppers on the S3. Fixed by moving to MCPWM (6 independent timers).
4. **All‑or‑nothing motor enable** — a single failed channel disabled all six. Fixed with per‑channel enable.
5. **Arduino‑core 3.x / IDF 5.x breakage** — the watchdog API changed shape; fixed with a version‑guarded init.

---

## 9. Known limits & TODO

- **No spare MCPWM timer** — all 6 are used; a 7th independent motor would need another approach.
- **Sequence slots are fixed‑size** — 8 slots × 32 steps, stored as a raw struct. A layout change invalidates saved sequences (a size mismatch reads as absent, so it degrades safely rather than corrupting).
- **Kill switches don't persist** — deliberate: a box power‑cycled after hours comes back with every arc live.
- **DAC false‑ACK** — the GP8403 library bit‑bangs I²C and can read a false ACK on a floating bus; the `test_dac` env scans the bus first to catch missing boards.
- **Supply** — the spec warns of inrush sag on the shared bulb rail; add bulk capacitance if brownout faults appear.
