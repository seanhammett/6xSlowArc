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
| **Mode switch** | Chooses Gallery or Performance (see below). The web page can do the same; whichever was used most recently is in charge. |
| **Sequence button** | In Performance mode, starts and stops the choreography. Does nothing in Gallery mode. The web page's Play / Stop button does the same. |
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
sequences are all remembered through a power cut — including a mode chosen on the web
page, whatever the switch says. A sequence that was playing does **not** restart by
itself: the box comes back in Performance, waiting, until someone presses Play or the
sequence button.

**One exception:** if you switched any arcs off from the web page, they come back **on**
after a power cycle. That is intentional — a box restarting overnight shouldn't come
back with an arc dark that nobody notices.

---

## 3. The two modes

Set with the physical switch on the box **or** the Gallery / Performance buttons at the
top of the web page's Control tab. Whichever was used last is in charge, so a box mounted
out of reach can be run entirely from the page (§6).

**Gallery** — each arc simply holds its own settings. Arc 1 stays at its brightness and
speed, arc 2 at its own, and so on, indefinitely. This is the normal day-to-day mode.

**Performance** — the box plays a **sequence**: a timed piece where arcs fade in and out
in a set order. The status light turns blue and blinks the moment the mode changes,
which is your confirmation it has taken. Press the sequence button (or **Play** on the
page) to start the piece; press again (or **Stop**) to stop. Play on the page also
switches to Performance if the box was in Gallery.

When you stop a sequence, the arcs **hold wherever they are** rather than jumping back.
Switch to Gallery to return them to their normal settings.

**When the page overrides the switch.** If the page sets a mode different from where the
switch sits, the page wins until the switch is next flipped. The switch then no longer
shows the box's real mode, so the status light adds a **brief white blip every two
seconds** on top of its normal pattern, and the page says which way the switch is set.
Either flip the switch (twice, if it has to end up where it already is) or choose the mode
on the page that matches the switch, and the blip stops.

---

## 4. Getting to the control page

The box hosts its own web page. You reach it from a phone, tablet, or laptop.

**If the box is on the gallery WiFi** — connect your device to the same network and go to
`http://slow-arc.local`. That name follows the box around, so you never need to know or
write down the number the router gave it. It works on Macs, iPhones, iPads, and current
Windows; on anything that can't resolve it, use the numeric address shown on the box's
status line instead, e.g. `http://192.168.1.47`.

The status line prints both, and the `slow-arc.local` line is a link — so once you're on
the page from any route, that's where to find the address again.

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
| **Blue blinking** | Healthy, in Performance mode, waiting. Press the sequence button. |
| Slow blue pulse | Healthy, sequence playing. |
| Amber pulsing | Looking for WiFi. Harmless — the arcs run regardless. |
| Fast blue flicker | A software update is being installed. Do not cut the power. |
| **Red flashes** | A fault. **Count the flashes** — see below. |
| White blip every 2 s, on top of any of the above | The web page has overridden the mode switch — the switch isn't showing the real mode. See §3. |

The light turns blue the instant the box goes into Performance, so you can
confirm the change has taken without waiting to see what the arcs do. It blinks
on and off while waiting and settles into a smooth breathing pulse once a
sequence is playing. (The blue flicker of a software update never goes fully
dark — that is how you tell the two apart, and updates only happen when someone
is deliberately installing one.)

Red flashes repeat in groups with a pause between:

- **2 flashes** — a bulb problem. One or more arcs may not light.
- **3 flashes** — a motor problem. One or more arcs may not turn.
- **4 flashes** — a power problem. Usually means the supply dipped when the bulbs came on.

Count the flashes and pass the number to whoever maintains the box — it tells them where
to look. A single red pattern that clears itself after a minute or so following a restart
is worth mentioning but not urgent.

---

## 6. The Control tab

The top strip is the box's status at a glance: healthy or faulty, which mode, which
sequence is selected and whether it is running, the WiFi, and how long it has been on.

Under the tabs, the **Playback** card does the job of the front panel: **Gallery** and
**Performance** choose the mode, and **▶ Play / ■ Stop** starts and stops the selected
sequence. The highlighted button is the box's current mode. If the page has overridden
the switch, an amber line says so (§3).

If the strip turns red and says **OFFLINE**, the page has lost contact with the box —
it has been powered down, or your phone has left the network. The sliders below grey
out and stop responding, because what they are showing is the last reading received
rather than anything current. The strip counts up how long it has been out of contact,
and comes back by itself within a few seconds of the box returning.

Below it, one column per arc. **Note that these values should not be adjusted except on 
explicit instructions from Conrad or his team.**

- The **blue slider (M)** is motor speed, 0–100.
- The **yellow slider (B)** is bulb brightness, 0–100.
- The **box under the two sliders** is the motor's speed in Hz — the actual step rate,
  60 to 8000. It is the real setting; the M slider is just a quick way to move it. Type a
  number and press Enter (or tap away) to set a speed exactly, e.g. to give two arcs the
  same rate or to return to a number written down at commissioning.
- The **on / off button** switches that arc off completely — dark and stopped — without
  losing its settings. Useful if a bulb blows or an arc needs attention mid-show. Press
  again to bring it back exactly as it was.

Changes take effect as you drag or as you leave the Hz box, and save themselves a second
or two later. There is no save button and nothing to confirm.

Set 0 to turn that half off: brightness 0 is dark, speed 0 is stopped. A typed speed
between 1 and 59 Hz is raised to 60 — below that the motor is switched off instead, so
there is no crawling-but-not-quite-stopped state.

![The Control tab](docs/img/ui-control.png)

---

## 7. Watching a sequence play

In Performance mode a timeline appears above the sliders.

![Performance mode with a sequence running](docs/img/ui-performance.png)

Each of the six rows is one arc. The yellow shape shows when that arc is lit; the red
line is the current position, sweeping left to right and looping. The header names the
piece being drawn and shows how far through you are — `5:32 / 11:08` above.

If you pick a different sequence while one is playing, the header says so —
*"FAST" starts on the next press* (of Play or the sequence button). The drawing keeps showing the piece that is
actually playing until you stop and start it again. Nothing has been lost; the box
is just refusing to cut a piece off part-way through.

Note that the sequence controls **when** each arc rises and falls, not how bright or fast
it goes. That still comes from the sliders. So you can re-tune the look of the piece at
any time without touching the choreography.

---

## 8. The Sequences tab

![The Sequences tab](docs/img/ui-sequences.png)

Three sections.

**Active sequence** — pick which piece plays. If a sequence is playing when you change
this, the new one starts at the **next** press of Play or the sequence button, so you never cut a
piece off mid-way. The line under the menu tells you which of the two happened, and the
Control tab names the selected piece in its status strip.

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
   has joined, and an **Address** line appears showing `http://slow-arc.local`.

Your phone drops off the `SlowArc-Setup` network about a minute after the join succeeds.
Rejoin the gallery WiFi and go to `http://slow-arc.local` — no need to have noted the
number.

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

**The page says OFFLINE or won't load.** Either the box is powered down or your phone
has dropped off the network. Check the box's status light first: if it is lit, the box
is fine and the problem is at your end — reconnect and the page recovers by itself. The
arcs keep running regardless; the page going away doesn't affect the artwork.

While the page says OFFLINE the sliders are greyed out on purpose. They are showing the
last values received, not what the box is doing, so they are not safe to drag.

**The status light is flashing red.**

![A fault shown on the status strip](docs/img/ui-fault.png)

Count the flashes (§5) and note the `FAULT` number on the web page. Both say the same
thing. Report the number.

**Everything is dark after a power cut.** Give it a minute. The bulbs fade up slowly and
deliberately on every start.

**The sequence won't start.** Check the mode is Performance — the sequence button does
nothing in Gallery mode. **Play** on the page works from either mode.

**The mode switch seems to do nothing, or the light has a white blip.** The page has set
the mode since the switch was last moved. The switch takes over again as soon as it is
flipped — if it is already where you want it, flip it away and back.

**A sequence you saved isn't playing.** Saving a sequence doesn't select it. Go to the
Sequences tab and choose it under *Active sequence*, then press Play or the sequence button.

---

## Quick reference

| | |
|---|---|
| Setup network | `SlowArc-Setup` / `slowarc123` |
| Page address on that network | `http://192.168.4.1` |
| Page address on gallery WiFi | `http://slow-arc.local` (or the number on the status line) |
| Healthy light | slow green pulse (Gallery), blue blinking (Performance, waiting), slow blue pulse (sequence playing) |
| Red 2 / 3 / 4 flashes | bulb / motor / power |
| White blip every 2 s | web page has overridden the mode switch |
| Settings saved automatically | yes — brightness, speed, mode, sequences |
| Survives a power cut | everything, except arcs switched off from the page |

---

*The screenshots in this guide were taken from the controller's own web page. The
readings shown in them — network names, addresses, uptime, slider positions — are
examples, not readings from your box.*
