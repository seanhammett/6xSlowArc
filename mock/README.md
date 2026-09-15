# Mock web UI — the controls, with no box attached

`index.html` is the firmware's web UI running against a **simulated** Slow Arc
Controller. Double-click it (or drag it into a browser) — no server, no network,
no hardware. Everything works: the sliders, the Hz boxes, the per-arc on/off,
the Sequences tab, the WiFi scan and join, faults, the OFFLINE state.

Made for screen-recording the controls when the rig isn't on the bench.

## How it's built

`tools/make_mock_ui.py` lifts the page **verbatim** out of the `kIndexHtml`
literal in [`../src/WebUi.cpp`](../src/WebUi.cpp) and injects
[`../tools/mock_backend.js`](../tools/mock_backend.js) ahead of the page's own
script. The mock replaces `window.fetch` with an in-memory implementation of
every endpoint (`/api/state`, `/api/sequence`, `/api/seqs`, `/api/seq`,
`/api/seq/select`, `/api/seq/save`, `/api/set`, `/api/mode`, `/api/run`, `/api/arc`, `/api/scan`,
`/api/wifi`), including its own copy of `SequenceEngine::expand`, so what you
record is pixel-for-pixel what the box serves.

**Do not edit `index.html`** — it is generated. Change the UI in `WebUi.cpp`,
the simulation in `mock_backend.js`, then:

```bash
python3 tools/make_mock_ui.py
```

## Recording it

The bottom-right **DEMO** panel is not part of the product — it stands in for
the things you'd do at the box itself:

| Control | Stands for |
|---|---|
| **Mode switch** | The physical Gallery / Performance toggle |
| **Sequence button** | The physical start/stop button (Performance only) |
| **Playhead** | Scrub the running sequence — the only way to show the middle of an 11-minute piece without waiting for it |
| **Fault** | Raise fault 2 (bulb/DAC), 3 (motor) or 4 (supply) to show the red status line |
| **Network** | Station vs. the `SlowArc-Setup` SoftAP (changes the address shown) |
| **Power** | "cut" stops answering, so the page's OFFLINE banner and greyed-out controls appear after ~5 s |
| **reset demo** | Back to the opening state without a reload |

Press <kbd>`</kbd> (backtick) to collapse the panel to a small ⚙ in the corner,
or open `index.html?panel=0` for a clean take with no panel at all.

## What's simulated

- **Set-points** start at a plausibly commissioned rig, not the firmware's
  first-boot defaults (40 / 350 Hz), which put every slider on the floor.
- **Sequences**: slot 0 is the real built-in `Original` (11:08 loop); `Short
  Demo` (0:38) exists so the playhead visibly crosses the timeline on camera;
  `Outside In` gives the dropdown a third choice. Saving, overwrite-by-name,
  the 8-slot limit and queue-on-next-press all behave as the firmware does.
- **WiFi scan** answers `202 scanning` for ~2.6 s before returning the list,
  same as the async scan on the box. A join spends 5 s "connecting".
- Nothing persists. Reload for a fresh take.
