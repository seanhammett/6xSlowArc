# Sequences as CSV

Import these on the web UI's **Sequences** tab (**Import CSV…**, then **Save**). The
format is the one **Export CSV** writes: `time` in seconds, one column per arc
(`1` = lit), `ramp` — the fade into that row's state, in seconds — and a closing `END`
row whose time sets the loop length. See §8 of the [user guide](../USER_GUIDE.md).

| File | What it is |
|---|---|
| `Sequence - LONG.csv` | The artist's 40:00 piece: 900 steps, 0.5 s ramps, loop 2400 s. |
| `Original.csv` | The built-in piece, step for step as the firmware seeds it (4 s ramps, 11:08 loop). |
| `test-1000-steps-40min.csv` | 1000 random steps with mixed ramps (0–2 s), loop 2400 s: exercises the step limit and pairs with the 40:00 track. |
| `test-loop-20s.csv` | A 20 s loop in the `time,mask,ramp` layout, to pair with a 20 s audio clip (`audio/test-loop-20s.m4a`) for checking audio sync and the loop wrap quickly. |
