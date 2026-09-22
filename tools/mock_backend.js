//
// mock_backend.js — a simulated Slow Arc Controller, in the browser.
//
// Injected ahead of the page's own script by tools/make_mock_ui.py, this file
// replaces window.fetch with an in-memory implementation of every endpoint in
// WebUi.cpp. The UI itself is byte-identical to the firmware's, so a screen
// capture of mock/index.html shows exactly what the real box serves — no
// hardware, no network, no ESP32.
//
// It also adds a "director's panel" (bottom-right) for the things an operator
// does at the box itself: throw the mode switch, press the sequence button,
// scrub the playhead, raise a fault, cut the power. Press ` (backtick) to hide
// it for a clean take, or open the page with ?panel=0 to leave it out entirely.
//
// State lives in memory only — reload for a fresh take.
//

(function () {
  'use strict';

  // --- Mirrors of config.h ---------------------------------------------------
  const N = 6;
  const MOTOR_MIN_SPEED_HZ = 60;
  const MOTOR_MAX_SPEED_HZ = 8000;
  const SEQ_SLOTS = 8;
  const SEQ_MAX_STEPS = 1000;

  // --- The simulated box -----------------------------------------------------
  // Set-points are a plausibly commissioned rig rather than ChannelModel's
  // first-boot defaults (40 / 350 Hz), which put every slider on the floor and
  // make for a dull recording.
  const FRESH = () => ({
    brightness: [184, 168, 205, 150, 176, 193],   // 0..255, as NVS holds them
    speedHz:    [420, 560, 380, 640, 500, 300],   // step rate, Hz
    on:         [1, 1, 1, 1, 1, 1],               // per-arc kill switches
    mode:       'Gallery',                        // the box's mode (model.mode)
    switchPos:  'Gallery',                        // where the panel switch sits
    fault:      0,                                // 0 none, 2 DAC, 3 motor, 4 supply
    wifi:       'station',                        // station | ap | connecting
    ssid:       'Gallery-WiFi',
    ip:         '192.168.1.XX',
    host:       'slow-arc.local',
    powered:    true,                             // false = box switched off
    uptimeBase: 3617,                             // "has been up a while"
  });

  const dev = FRESH();
  const bootAt = Date.now();

  // --- Stored sequences (SequenceStore) --------------------------------------
  // Slot 0 is the firmware's built-in piece, seeded verbatim. The other two are
  // there so the Sequences tab has something to choose between on camera; the
  // short one loops in under a minute, which is the only way the playhead
  // visibly crosses the timeline in a screen capture.
  const FRESH_SEQS = () => [
    { name: 'Original', ramp: 4, steps: [
      { t: 4,   m: 0b000001 }, { t: 64,  m: 0b000011 }, { t: 124, m: 0b000111 },
      { t: 184, m: 0b001111 }, { t: 244, m: 0b011111 }, { t: 304, m: 0b111111 },
      { t: 364, m: 0b111110 }, { t: 424, m: 0b111100 }, { t: 484, m: 0b111000 },
      { t: 544, m: 0b110000 }, { t: 604, m: 0b100000 }, { t: 664, m: 0b000000 },
    ] },
    { name: 'Short Demo', ramp: 2, steps: [
      { t: 6,  m: 0b000011 }, { t: 12, m: 0b001100 }, { t: 18, m: 0b110000 },
      { t: 24, m: 0b111111 }, { t: 30, m: 0b010101 }, { t: 36, m: 0b000000 },
    ] },
    { name: 'Outside In', ramp: 6, steps: [
      { t: 20,  m: 0b100001 }, { t: 60,  m: 0b110011 }, { t: 100, m: 0b111111 },
      { t: 140, m: 0b011110 }, { t: 180, m: 0b001100 }, { t: 220, m: 0b000000 },
    ] },
    null, null, null, null, null,
  ];

  // Literals above are in seconds for readability; the box keeps ms.
  const toMs = d => d && { name: d.name, rampMs: d.ramp * 1000,
                           steps: d.steps.map(s => ({ t: s.t * 1000, m: s.m })) };
  let seqs = FRESH_SEQS().map(toMs);
  let activeSlot = 0;

  // --- SequenceEngine --------------------------------------------------------
  // The box evaluates frames straight from the steps (SeqFrame.h); the page
  // draws them itself, so all the mock needs is the piece and its loop length
  // (last step + ramp).
  const loopLen = d => d && d.steps.length ? d.steps[d.steps.length - 1].t + d.rampMs : 0;
  const seqJson = d => ({ name: d ? d.name : '', ramp_ms: d ? d.rampMs : 0, len: loopLen(d),
                          steps: d ? d.steps.map(s => [s.t, s.m]) : [] });

  const eng = { def: null, len: 0, queued: null, running: false, startAt: 0, runId: 0 };

  function engApply(def) {
    if (eng.running) { eng.queued = def; return; }  // takes effect on next start
    eng.def = def; eng.len = loopLen(def);
  }
  function engStart() {
    if (eng.queued) { eng.def = eng.queued; eng.len = loopLen(eng.queued); eng.queued = null; }
    eng.runId++;
    eng.running = true;
    eng.startAt = Date.now();
  }
  function engPosMs() {
    if (!eng.running || !eng.len) return 0;
    return (Date.now() - eng.startAt) % eng.len;
  }
  engApply(seqs[activeSlot]);

  // motorClampHz(): anything between a stop and the floor becomes the floor.
  const clampHz = h => !isFinite(h) || h <= 0 ? 0
    : Math.max(MOTOR_MIN_SPEED_HZ, Math.min(MOTOR_MAX_SPEED_HZ, Math.round(h)));

  // --- Endpoint handlers -----------------------------------------------------
  const json = (obj, status = 200) => new Response(JSON.stringify(obj),
    { status, headers: { 'Content-Type': 'application/json' } });
  const text = (s, status) => new Response(s,
    { status, headers: { 'Content-Type': 'text/plain' } });

  function stateJson() {
    return {
      brightness: dev.brightness.slice(),
      speed: dev.speedHz.slice(),
      on: dev.on.slice(),
      mode: dev.mode,
      override: dev.mode !== dev.switchPos,
      running: eng.running,
      seq_t: engPosMs(),
      seq_len: eng.len,
      run_id: eng.runId,
      seq_name: eng.def ? eng.def.name : '',
      seq_queued: eng.queued ? eng.queued.name : '',
      wifi: dev.wifi,
      ssid: dev.wifi === 'ap' ? 'SlowArc-Setup' : dev.ssid,
      ip: dev.wifi === 'ap' ? '192.168.4.1' : (dev.wifi === 'station' ? dev.ip : ''),
      // mDNS only routes on the house network, so main.cpp reports it empty
      // unless the box is a station.
      host: dev.wifi === 'station' ? dev.host : '',
      fault: dev.fault,
      uptime: dev.uptimeBase + Math.floor((Date.now() - bootAt) / 1000),
    };
  }

  // WiFi scan: the firmware answers 202 while the async scan runs, then 200 with
  // the list. Reproduced so the "scanning…" state is recordable.
  let scanStartedAt = 0;
  const SCAN_MS = 2600;
  const NETS = [
    { ssid: 'Gallery-WiFi',  rssi: -49, open: false },
    { ssid: 'Gallery-Guest', rssi: -55, open: true  },
    { ssid: 'Studio-5G',     rssi: -67, open: false },
    { ssid: '',              rssi: -70, open: false },   // hidden — the UI skips it
    { ssid: 'BT-8H2K9J',     rssi: -76, open: false },
    { ssid: 'VM3928104',     rssi: -83, open: false },
  ];

  const HANDLERS = {
    'GET /api/state': () => json(stateJson()),

    'GET /api/sequence': () => json(seqJson(eng.def)),

    'GET /api/seqs': () => json({
      active: activeSlot,
      seqs: seqs.map((s, i) => s && { i, name: s.name }).filter(Boolean),
    }),

    'GET /api/seq': q => {
      const i = parseInt(q.get('i'), 10);
      if (!(i >= 0 && i < SEQ_SLOTS) || !seqs[i]) return text('no such sequence', 404);
      return json(seqJson(seqs[i]));
    },

    'POST /api/seq/select': q => {
      const i = parseInt(q.get('i'), 10);
      if (!(i >= 0 && i < SEQ_SLOTS) || !seqs[i]) return text('no such sequence', 404);
      activeSlot = i;
      engApply(seqs[i]);
      return json({ ok: true });
    },

    // Name + ramp in the query, "t_ms:mask,..." in the body — parsed and
    // rejected the way WebUi.cpp's parseSeqSave does.
    'POST /api/seq/save': (q, body) => {
      if (!q.has('name') || !q.has('ramp_ms')) return text('missing name/ramp_ms', 400);
      if (typeof body !== 'string' || !body.length) return text('missing steps (or body over 16 KB)', 400);
      const name = q.get('name').slice(0, 23);      // SeqDef::name is char[24]
      if (!name) return text('missing name', 400);
      const snap = ms => Math.round(Math.max(0, ms) / 100) * 100;
      const rampMs = snap(Math.min(600000, Math.max(100, parseInt(q.get('ramp_ms'), 10) || 0)));
      const steps = [];
      for (const part of body.trim().split(',')) {
        if (steps.length >= SEQ_MAX_STEPS) return text('too many steps (max 1000)', 400);
        const m = /^(-?\d+):(\d+)$/.exec(part.trim());
        if (!m) return text('bad step (want t_ms:mask)', 400);
        if (+m[2] > 63) return text('bad step mask (0..63)', 400);
        steps.push({ t: snap(parseInt(m[1], 10)), m: +m[2] });
      }
      if (!steps.length) return text('no steps', 400);
      steps.sort((a, b) => a.t - b.t);               // stable, like the firmware's
      const def = { name, rampMs, steps };
      // saveByName: same name overwrites, otherwise the first free slot.
      let slot = seqs.findIndex(s => s && s.name === name);
      if (slot < 0) slot = seqs.findIndex(s => !s);
      if (slot < 0) return text('sequence slots full (or storage write failed)', 507);
      seqs[slot] = def;
      if (slot === activeSlot) engApply(def);
      return json({ ok: true, slot });
    },

    'POST /api/set': q => {
      if (!q.has('ch')) return text('missing ch', 400);
      const ch = parseInt(q.get('ch'), 10);
      if (!(ch >= 0 && ch < N)) return text('bad ch', 400);
      if (q.has('brightness'))
        dev.brightness[ch] = Math.min(255, Math.max(0, parseInt(q.get('brightness'), 10) || 0));
      if (q.has('speed'))
        dev.speedHz[ch] = clampHz(parseInt(q.get('speed'), 10));
      return json({ ok: true });
    },

    // Mode + playback, as main.cpp's loop applies them: last change wins against
    // the switch, leaving Performance stops the engine, Play implies Performance
    // and doesn't restart a piece already playing.
    'POST /api/mode': q => {
      const m = q.get('m');
      if (m !== 'Gallery' && m !== 'Performance')
        return text('m must be Gallery or Performance', 400);
      dev.mode = m;
      if (m === 'Gallery') eng.running = false;
      syncPanel();
      return json({ ok: true });
    },

    'POST /api/run': q => {
      if (!q.has('on')) return text('missing on', 400);
      if (parseInt(q.get('on'), 10) !== 0) {
        dev.mode = 'Performance';
        if (!eng.running) engStart();
      } else {
        eng.running = false;
      }
      syncPanel();
      return json({ ok: true });
    },

    'POST /api/arc': q => {
      if (!q.has('ch') || !q.has('on')) return text('missing ch/on', 400);
      const ch = parseInt(q.get('ch'), 10);
      if (!(ch >= 0 && ch < N)) return text('bad ch', 400);
      dev.on[ch] = parseInt(q.get('on'), 10) !== 0 ? 1 : 0;
      return json({ ok: true });
    },

    'GET /api/scan': () => {
      if (!scanStartedAt) { scanStartedAt = Date.now(); return json({ scanning: true }, 202); }
      if (Date.now() - scanStartedAt < SCAN_MS) return json({ scanning: true }, 202);
      scanStartedAt = 0;                            // WiFi.scanDelete()
      return json(NETS);
    },

    // Joining walks the box through "connecting" and out the other side, which
    // is the whole point of recording this card.
    'POST /api/wifi': q => {
      if (!q.has('ssid')) return text('missing ssid', 400);
      dev.ssid = q.get('ssid');
      dev.wifi = 'connecting';
      setTimeout(() => {
        if (dev.wifi !== 'connecting') return;      // the panel overrode us meanwhile
        dev.wifi = 'station';
        dev.ip = '192.168.1.57';
        syncPanel();
      }, 5000);
      syncPanel();
      return json({ ok: true });
    },
  };

  // --- The fetch shim --------------------------------------------------------
  // A dead box does not answer: reject, exactly as a real failed request does,
  // so what you see is the page's own OFFLINE handling.
  const realFetch = window.fetch ? window.fetch.bind(window) : null;
  window.fetch = function (input, init) {
    const url = typeof input === 'string' ? input : (input && input.url) || '';
    const u = new URL(url, 'http://slow-arc.local/');
    const handler = HANDLERS[((init && init.method) || 'GET').toUpperCase() + ' ' + u.pathname];
    if (!handler) {
      return realFetch ? realFetch(input, init)
                       : Promise.reject(new TypeError('Failed to fetch'));
    }
    return new Promise((resolve, reject) => {
      const fail = () => reject(new TypeError('Failed to fetch'));
      if (!dev.powered) { setTimeout(fail, 400); return; }
      // A few ms of latency keeps the optimistic UI (the on/off buttons) honest.
      setTimeout(() => {
        if (!dev.powered) { fail(); return; }
        try { resolve(handler(u.searchParams, init && init.body)); } catch (e) { fail(); }
      }, 25 + Math.random() * 45);
    });
  };

  // --- Director's panel ------------------------------------------------------
  let panel = null, pill = null;
  function syncPanel() {
    if (!panel) return;
    const set = (id, on) => panel.querySelector(id).classList.toggle('on', on);
    set('#mkGallery', dev.switchPos === 'Gallery');     // the switch, not the mode —
    set('#mkPerf',    dev.switchPos === 'Performance'); // the web can override it
    set('#mkSta',     dev.wifi === 'station' || dev.wifi === 'connecting');
    set('#mkAp',      dev.wifi === 'ap');
    set('#mkOn',      dev.powered);
    set('#mkOff',    !dev.powered);
    panel.querySelector('#mkfault').value = String(dev.fault);

    const press = panel.querySelector('#mkPress');
    press.disabled = dev.mode !== 'Performance';
    press.textContent = eng.running ? 'press — stop' : 'press — start';

    const pos = panel.querySelector('#mkpos'), len = eng.len;
    pos.disabled = !len;
    if (len && document.activeElement !== pos)
      pos.value = Math.round(engPosMs() / len * 1000);
    const mmss = ms => Math.floor(ms / 60000) + ':' +
                       String(Math.floor(ms / 1000) % 60).padStart(2, '0');
    panel.querySelector('#mkposlbl').textContent =
      len ? `${mmss(engPosMs())} / ${mmss(len)}` : '';
  }

  if (new URLSearchParams(location.search).get('panel') === '0') return;

  const CSS = `
  #mkpanel,#mkpill{position:fixed;right:12px;bottom:12px;z-index:9999;
    font:12px/1.35 ui-monospace,SFMono-Regular,Menlo,monospace}
  #mkpill{width:30px;height:30px;border-radius:50%;background:#2b3140;border:1px solid #566;
    color:#9ad;display:none;align-items:center;justify-content:center;cursor:pointer;user-select:none}
  #mkpanel{width:222px;background:#101218;border:1px solid #3a4152;border-radius:10px;
    padding:.55rem .6rem;color:#cdd3de;box-shadow:0 6px 24px rgba(0,0,0,.5)}
  #mkpanel.hidden{display:none}
  #mkpanel h4{margin:0 0 .45rem;font-size:11px;letter-spacing:.06em;color:#7d8697;font-weight:600;
    display:flex;justify-content:space-between;align-items:center}
  #mkpanel .r{margin:.4rem 0}
  #mkpanel .lbl{color:#7d8697;font-size:10px;letter-spacing:.05em;margin-bottom:.15rem}
  #mkpanel button{background:#1b2028;border:1px solid #3a4152;border-radius:5px;color:#cdd3de;
    padding:.22rem .45rem;font:inherit;cursor:pointer}
  #mkpanel button:hover:not(:disabled){border-color:#5c6a80}
  #mkpanel button.on{background:#26456b;border-color:#4a90d9;color:#dbe8f7}
  #mkpanel button:disabled{opacity:.4;cursor:default}
  #mkpanel .seg{display:flex;gap:.25rem}
  #mkpanel .seg button{flex:1}
  #mkpanel select{background:#1b2028;border:1px solid #3a4152;border-radius:5px;color:#cdd3de;
    padding:.2rem .3rem;font:inherit;width:100%}
  /* The page styles range inputs as tall vertical faders; put these back. */
  #mkpanel input[type=range]{width:100%;height:auto;margin:0;accent-color:#4a90d9;
    writing-mode:horizontal-tb;direction:ltr;-webkit-appearance:auto;appearance:auto}
  #mkpanel .x{background:none;border:none;color:#7d8697;padding:0;font:inherit}
  #mkfoot{color:#6c7484;font-size:10px;margin-top:.5rem;border-top:1px solid #262c38;padding-top:.35rem}
  `;

  const HTML = `
  <h4><span>DEMO · simulated box</span><button class="x" id="mkclose" title="hide (\`)">✕</button></h4>
  <div class="r"><div class="lbl">MODE SWITCH</div>
    <div class="seg"><button id="mkGallery">Gallery</button><button id="mkPerf">Performance</button></div></div>
  <div class="r"><div class="lbl">SEQUENCE BUTTON</div>
    <div class="seg"><button id="mkPress">press</button></div></div>
  <div class="r"><div class="lbl">PLAYHEAD <span id="mkposlbl"></span></div>
    <input type="range" id="mkpos" min="0" max="1000" value="0"></div>
  <div class="r"><div class="lbl">FAULT</div>
    <select id="mkfault">
      <option value="0">none — healthy</option>
      <option value="2">2 — bulb / DAC</option>
      <option value="3">3 — motor</option>
      <option value="4">4 — supply / brownout</option>
    </select></div>
  <div class="r"><div class="lbl">NETWORK</div>
    <div class="seg"><button id="mkSta">station</button><button id="mkAp">SoftAP</button></div></div>
  <div class="r"><div class="lbl">POWER</div>
    <div class="seg"><button id="mkOn">on</button><button id="mkOff">cut</button></div></div>
  <div class="r"><div class="seg"><button id="mkReset">reset demo</button></div></div>
  <div id="mkfoot">\` hides this · ?panel=0 omits it</div>
  `;

  function toggleShown() {
    const hidden = panel.classList.toggle('hidden');
    pill.style.display = hidden ? 'flex' : 'none';
  }

  function buildPanel() {
    const style = document.createElement('style');
    style.textContent = CSS;
    document.head.appendChild(style);

    panel = document.createElement('div');
    panel.id = 'mkpanel';
    panel.innerHTML = HTML;
    document.body.appendChild(panel);

    pill = document.createElement('div');
    pill.id = 'mkpill';
    pill.textContent = '⚙';
    pill.title = 'show the demo panel (`)';
    pill.addEventListener('click', toggleShown);
    document.body.appendChild(pill);

    const on = (id, fn) => panel.querySelector(id).addEventListener('click', fn);
    // A flip sets the mode (last change wins); re-clicking the current position
    // is no flip, just as a switch already there can't be thrown again.
    on('#mkGallery', () => {
      if (dev.switchPos === 'Gallery') return;
      dev.switchPos = dev.mode = 'Gallery';
      eng.running = false;            // main.cpp stops the engine leaving Performance
      syncPanel();
    });
    on('#mkPerf', () => {
      if (dev.switchPos === 'Performance') return;
      dev.switchPos = dev.mode = 'Performance';
      syncPanel();
    });
    on('#mkPress', () => { eng.running ? (eng.running = false) : engStart(); syncPanel(); });
    on('#mkSta',   () => { dev.wifi = 'station'; dev.ip = '192.168.1.57'; syncPanel(); });
    on('#mkAp',    () => { dev.wifi = 'ap'; syncPanel(); });
    on('#mkOn',    () => { dev.powered = true; syncPanel(); });
    on('#mkOff',   () => { dev.powered = false; syncPanel(); });
    on('#mkclose', toggleShown);
    on('#mkReset', () => {
      Object.assign(dev, FRESH());
      seqs = FRESH_SEQS().map(toMs);
      activeSlot = 0;
      eng.running = false;
      eng.queued = null;
      engApply(seqs[0]);
      scanStartedAt = 0;
      syncPanel();
    });

    panel.querySelector('#mkfault').addEventListener('change', e => {
      dev.fault = parseInt(e.target.value, 10) || 0;
    });
    // Scrubbing rebases the engine's start time, so the page goes on
    // extrapolating the playhead at 1x from wherever you drop it.
    panel.querySelector('#mkpos').addEventListener('input', e => {
      if (!eng.len) return;
      if (!eng.running) engStart();
      eng.startAt = Date.now() - Math.round(+e.target.value / 1000 * eng.len);
      syncPanel();
    });

    document.addEventListener('keydown', e => {
      if (e.key !== '`' || e.metaKey || e.ctrlKey || e.altKey) return;
      const t = e.target;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT')) return;
      e.preventDefault();
      toggleShown();
    });

    setInterval(syncPanel, 250);
    syncPanel();
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', buildPanel);
  else
    buildPanel();
})();
