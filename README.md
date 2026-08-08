# Slow Arc Controller — Firmware

ESP32-S3 firmware for a single control box driving six kinetic light artworks
("Slow Arcs"). Each Slow Arc = one dimmable 24V halogen bulb-pair + one geared
TMC2209 stepper. Built per [`slow_arc_controller_spec.md`](slow_arc_controller_spec.md).

## Build & flash

```bash
pio run                     # build
pio run -t upload           # flash over USB
pio device monitor          # serial console @ 115200
```

OTA (once on-network): set `upload_protocol = espota` / `upload_port = slow-arc.local`
in `platformio.ini`, then `pio run -t upload`.

Set WiFi credentials in [`src/wifi_config.h`](src/wifi_config.h) before deployment.
With them blank (or if the join fails) the box raises a SoftAP **`SlowArc-Setup`**
(password `slowarc123`) so the web UI is reachable for commissioning. WiFi is
**optional to operation** — the box runs its stored NVS config with the network down.

## Module map (`src/`)

| Module | Role |
|---|---|
| `ChannelModel` | The 12 set-points (6 brightness + 6 speed) + mode. Persisted source of truth. |
| `ConfigStore` | NVS persistence (debounced); restores on boot, falls back to defaults. |
| `BulbController` | Owns 3× GP8403 DACs → 6 DIM14. Gamma curve, soft-start, stagger. |
| `MotorController` | Owns 6× FastAccelStepper. Ramped speed, EN lines, safe shutdown. |
| `InputManager` | Debounces (Bounce2) the Mode switch and Sequence button. |
| `SequenceEngine` | Performance-mode timeline interpreter (placeholder choreography). |
| `StatusLed` | WS2812 state indication. |
| `NetworkManager` | Non-blocking WiFi (STA + AP fallback, bounded reconnect). |
| `OtaService` | ArduinoOTA; safe-states the rig before flashing. |
| `WebUi` | Async web status/control page + JSON API. |
| `main.cpp` | Wiring, control loop, watchdog, brownout reporting, state JSON. |

Tunables live in [`src/config.h`](src/config.h); GPIO in [`src/pins.h`](src/pins.h).

## Web UI without the hardware

[`mock/index.html`](mock/README.md) is the real UI (lifted verbatim from
`WebUi.cpp`) driven by a simulated box, for demos and screen recordings when the
rig isn't on the bench. Open it straight from disk — no server, no ESP32.
Regenerate after any UI change with `python3 tools/make_mock_ui.py`.

## Operating modes

- **Gallery** (default): each channel holds its commissioned brightness/speed.
  Set-and-forget. The Mode switch selects it (switch open).
- **Performance**: `SequenceEngine` drives values over time; the Sequence button
  starts/stops playback. Switch closed selects it. **Stop holds the current
  values** (`SEQUENCE_STOP_HOLD` in `config.h`).

## Status-LED codes (WS2812)

| State | Indication |
|---|---|
| Boot / soft-start | Solid amber |
| Gallery, healthy | Slow green pulse |
| Performance, running | Slow blue pulse |
| WiFi connecting | Amber pulse |
| OTA in progress | Fast blue pulse |
| Fault | Red blink; count = subsystem (**2** bulb/DAC, **3** motor, **4** supply/brownout) |

Keep this table on the box — the LED is the only on-board diagnostic.

## Reliability (spec §8)

- **Soft-start**: each channel ramps 0→target over `BULB_RAMP_MS` (~350 ms).
- **Stagger**: channel turn-on offset by `BULB_STAGGER_MS` so twelve cold
  filaments don't inrush together on the 1000W rail.
- **Gamma**: configured brightness is gamma-corrected to perceptually-linear
  output, quantised to 256 levels.
- **Motors ramped**: FastAccelStepper accel/decel; never instantaneous jumps.
- **Safe shutdown**: any fault / watchdog reset leaves EN disabled. EN defaults
  disabled at boot (active-LOW + external pull-up).
- **State recovery**: 12 set-points + mode restored from NVS and soft-started.
- **Brownout**: reset reason is checked at boot and reported as fault code 4 —
  if you see it, the logic supply needs its own bulk cap (a hardware fix).

## Persistence model

NVS is the single source of persisted state. `ConfigStore` writes a versioned
binary blob; web-UI changes mark it dirty and are coalesced
(`NVS_SAVE_DEBOUNCE_MS`) so a slider drag never hammers flash.

## Open items (spec §9) — current decisions

1. **Sequence content**: placeholder loop in `SequenceEngine.cpp`; replace
   `kPlaceholderCues` (or call `load()`) with the artist's cues.
2. **Stop behaviour**: hold current values (configurable).
3. **GPIO**: assigned in `pins.h` for the DevKitC-1 — re-verify against the real board.
4. **Bulb part**: affects inrush documentation only; soft-start covers it.
5. **WiFi provisioning**: hardcoded creds + SoftAP fallback for install.

> Hardware reminders mirrored in `pins.h`: 10k pull-DOWN on each STEP, 10k
> pull-UP on each EN, and a dedicated bulk cap / separate supply for the logic
> rail so bulb inrush can't reset the brain.
