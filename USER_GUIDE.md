# Slow Arc — User Guide

For anyone running the installation day to day. No technical knowledge needed.

The controller is a small box that runs **six Slow Arcs**. Each arc has a pair of bulbs
that fade up and down, and a motor that turns it slowly. The box remembers everything
you set, and keeps running on its own whether or not there is WiFi.

---

## 1. The box

> **[ photo placeholder ]** — `docs/img/front-panel.jpg`
> The front panel: the mode switch, the sequence button, and the status light.

> **[ photo placeholder ]** — `docs/img/box-installed.jpg`
> The box in place, showing where the arcs connect.

There are only three things on the outside:

| | What it does |
|---|---|
| **Mode switch** | Chooses Gallery or Performance (see below). This switch always wins — the web page cannot override it. |
| **Sequence button** | In Performance mode, starts and stops the choreography. Does nothing in Gallery mode. |
| **Status light** | A small coloured light telling you the box is healthy. See §5. |

---

## 2. Turning it on

Switch on the power. Then:

1. The status light comes on **solid amber** for a few seconds.
2. The bulbs fade up gently — never all at once, and never instantly. A slow, staggered
   rise is normal and deliberate; it protects the bulbs and the power supply.
3. The motors start turning.
4. The light settles into a **slow green pulse**. The box is running normally.

Everything comes back exactly as you left it. Brightness, speed, mode, and your
sequences are all remembered through a power cut.

**One exception:** if you switched any arcs off from the web page, they come back **on**
after a power cycle. That is intentional — a box restarting overnight shouldn't come
back with an arc dark that nobody notices.

---

## 3. The two modes

Set with the physical switch on the box.

**Gallery** — each arc simply holds its own settings. Arc 1 stays at its brightness and
speed, arc 2 at its own, and so on, indefinitely. This is the normal day-to-day mode.

**Performance** — the box plays a **sequence**: a timed piece where arcs fade in and out
in a set order. Press the sequence button to start it; press again to stop.

When you stop a sequence, the arcs **hold wherever they are** rather than jumping back.
Flip the switch to Gallery to return them to their normal settings.

---

## 4. Getting to the control page

The box hosts its own web page. You reach it from a phone, tablet, or laptop.

**If the box is on the gallery WiFi** — connect your device to the same network and go to
the address shown on the box's status line, e.g. `http://192.168.1.47`. On Macs, iPhones,
and iPads `http://slow-arc.local` usually works too.

**If the box is not on any network** (a new box, or the WiFi is down) — it makes its own
network:

- **Network name:** `SlowArc-Setup`
- **Password:** `slowarc123`

Join that from your phone and the control page should open by itself. If it doesn't, go
to `http://192.168.4.1`.

This always works, even with no internet in the building, so you can always reach the box
on site.

---

## 5. The status light

| Light | Meaning |
|---|---|
| Solid amber | Starting up. Normal for the first few seconds. |
| Slow green pulse | Healthy, in Gallery mode. |
| Slow blue pulse | Healthy, sequence playing. |
| Amber pulsing | Looking for WiFi. Harmless — the arcs run regardless. |
| Fast blue flicker | A software update is being installed. Do not cut the power. |
| **Red flashes** | A fault. **Count the flashes** — see below. |

Red flashes repeat in groups with a pause between:

- **2 flashes** — a bulb problem. One or more arcs may not light.
- **3 flashes** — a motor problem. One or more arcs may not turn.
- **4 flashes** — a power problem. Usually means the supply dipped when the bulbs came on.

Count the flashes and pass the number to whoever maintains the box — it tells them where
to look. A single red pattern that clears itself after a minute or so following a restart
is worth mentioning but not urgent.

---

## 6. The Control tab

The top strip is the box's status at a glance: healthy or faulty, which mode, whether a
sequence is running, the WiFi, and how long it has been on.

Below it, one column per arc. **Note that these values should not be adjusted except on 
explicit instructions from Conrad or his team.**

- The **blue slider (M)** is motor speed, 0–100.
- The **yellow slider (B)** is bulb brightness, 0–100.
- The number under M is the motor's actual speed in Hz. You can ignore it; it's there for
  setup.
- The **on / off button** switches that arc off completely — dark and stopped — without
  losing its settings. Useful if a bulb blows or an arc needs attention mid-show. Press
  again to bring it back exactly as it was.

Changes take effect as you drag, and save themselves a second or two later. There is no
save button and nothing to confirm.

Set 0 for a slider to turn that half off: brightness 0 is dark, speed 0 is stopped.

![The Control tab](docs/img/ui-control.png)

---

## 7. Watching a sequence play

In Performance mode a timeline appears above the sliders.

![Performance mode with a sequence running](docs/img/ui-performance.png)

Each of the six rows is one arc. The yellow shape shows when that arc is lit; the red
line is the current position, sweeping left to right and looping. The header shows how
far through you are — `5:32 / 11:08` above.

Note that the sequence controls **when** each arc rises and falls, not how bright or fast
it goes. That still comes from the sliders. So you can re-tune the look of the piece at
any time without touching the choreography.

---

## 8. The Sequences tab

![The Sequences tab](docs/img/ui-sequences.png)

Three sections.

**Active sequence** — pick which piece plays. If a sequence is playing when you change
this, the new one starts at the **next** press of the sequence button, so you never cut a
piece off mid-way.

**Preview** — the same timeline view, for the sequence you selected, so you can see its
shape before committing to it. The header gives its ramp and total length.

**Create / edit** — build a piece of your own:

1. Give it a **name**. Saving with the name of an existing sequence replaces that one, so
   use a new name if you want to keep both.
2. Set the **ramp** in seconds — how long every fade takes, both up and down. One value
   for the whole piece.
3. Add **steps**. A step is a moment in time and a set of arcs: *"at 4 minutes, arcs 1, 2
   and 3 are on."* Tick the arcs that should be lit at that moment. The box fades between
   consecutive steps for you.
4. **Save**. The drawing at the bottom updates as you work, so you can see the shape
   before saving.

The piece loops. After the last step it fades back to where it began and starts over.

To modify an existing piece rather than start blank, select it above and press **Load
selected**.

The box holds up to **8 sequences**, each up to **32 steps**.

The built-in sequence, **"Original"**, is the piece the arcs wake one at a time over about
five minutes, all six hold together, then they fall away in the same order — an eleven
minute loop.

---

## 9. Putting the box on the gallery WiFi

Open **WiFi setup** at the bottom of the Control tab.

![WiFi setup](docs/img/ui-wifi.png)

1. Press **Scan networks** and wait a few seconds.
2. Tap your network in the list. A padlock means it needs a password.
3. Type the password and press **Join**.
4. Watch the status line at the top. It changes to `station` with an address once the box
   has joined.

If the join fails, the `SlowArc-Setup` network stays up — rejoin it and try again. The
box will not strand you.

You only ever need to do this once. It is remembered, and the box reconnects by itself
after a power cut. **The arcs do not need WiFi to run** — it only matters for changing
settings from a phone.

---

## 10. If something looks wrong

**An arc is dark or not turning.** Check its on/off button on the Control tab — it may
have been switched off. Then check its sliders aren't at 0. Then check the status light
for red flashes.

**The page says "offline" or won't load.** Your phone has probably dropped off the
network. Reconnect it. The arcs keep running regardless — the page going away doesn't
affect the artwork.

**The status light is flashing red.**

![A fault shown on the status strip](docs/img/ui-fault.png)

Count the flashes (§5) and note the `FAULT` number on the web page. Both say the same
thing. Report the number.

**Everything is dark after a power cut.** Give it a minute. The bulbs fade up slowly and
deliberately on every start.

**The sequence won't start.** Check the mode switch is on Performance — the button does
nothing in Gallery mode.

**A sequence you saved isn't playing.** Saving a sequence doesn't select it. Go to the
Sequences tab and choose it under *Active sequence*, then press the sequence button.

---

## Quick reference

| | |
|---|---|
| Setup network | `SlowArc-Setup` / `slowarc123` |
| Page address on that network | `http://192.168.4.1` |
| Page address on gallery WiFi | shown on the box's status line, or `http://slow-arc.local` |
| Healthy light | slow green pulse (Gallery) or slow blue pulse (sequence playing) |
| Red 2 / 3 / 4 flashes | bulb / motor / power |
| Settings saved automatically | yes — brightness, speed, mode, sequences |
| Survives a power cut | everything, except arcs switched off from the page |

---

*The screenshots in this guide were taken from the controller's own web page. The
readings shown in them — network names, addresses, uptime, slider positions — are
examples, not readings from your box.*
