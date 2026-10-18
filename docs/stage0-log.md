# Stage 0 — Hypothesis, Criteria, and Log

> **Written before any Stage 0 code exists.** That is the entire point. Criteria decided
> after the fact get bent to fit whatever was built. Nothing below may be edited once
> implementation starts, except the Sessions and Verdict sections, which are append-only.

Governing design: [18 — Vertical Slice](18-vertical-slice.md).

---

## 1. The hypothesis

> A fight of 3–8 seconds against one ordinary enemy, where either party dies in 2–3 hits
> and attacks commit with no cancel, produces **tension** rather than frustration —
> **provided every wind-up is readable.**

The final clause is the confound. If the prototype feels bad, the cause is either the
design or the telegraphs, and those take opposite fixes. Projects routinely conclude
"lethal combat isn't fun" when what they built was *unreadable* combat.

## 2. What is being tested

Only this. Not whether the game is fun, not whether the world is interesting.

1. Is a single fight against one ordinary enemy tense?
2. Does the death-cause distribution point at the design rather than at telegraphs?
3. Do two outside testers die and immediately try again?

## 3. Starting parameters

Fixed at the outset so drift is visible. Changes get logged in §6.

| Player | |
|---|---|
| HP | 100 |
| Light / heavy damage | 30 / 50 |
| Wind-up, light / heavy | 0.35 s / 0.7 s |
| Recovery, light / heavy | 0.4 s / 0.8 s |
| Stamina | 100 |
| Cost: light / heavy / dodge | 20 / 35 / 25 |
| Stamina regen | 25/s after 1 s idle |
| Block | absorbs damage as stamina at 40% |
| **Dodge i-frames** | **none** |

| Skirmisher | |
|---|---|
| HP | 100 |
| Preferred spacing | 2.5 m |
| Desire threshold | 1.0; +0.35/s in band, +0.6/s vs recovering player |
| Rest trigger | 3 commits or 6 s |
| Rest duration | 1.5 s |
| Tell | 0.15 s |
| Tracking cap | 30°/s wind-up, 0°/s active |
| Punish reaction | 0.25 s |
| Stamina | 100, 30/attack, regen 15/s |

## 4. Measurement

Per fight: duration, HP remaining, damage taken, hits landed, whiffs, blocks, dodges,
time-to-retry.

**Every death is classified**, stamped by the state machine at the moment of the killing
blow rather than recalled afterwards:

| Cause | Meaning |
|---|---|
| `stamina` | Out of stamina when it mattered |
| `whiff_punished` | Killed during own recovery |
| `cornered` | No room to reposition |
| `mistimed_block` | Blocked, but late or wrong |
| `offscreen` | Attacker not on screen at wind-up |
| `unreadable` | Attack was on screen and still not reacted to |

**The first 20 fights are recorded.** Each fight is written to `<fights csv without extension>.fights/fight_NNN.ifrd` the moment it ends (via a temporary file and a rename), so a crash loses nothing already finished. Fights 21 onward are not kept. To review one: `stage0 --review-fight fights.fights/fight_007.ifrd` prints the outcome, the cause, a per-side summary and every swing, hit, block, whiff, dodge and stagger with its time. A truncated or corrupted file is refused with exit code 1.

**Replay regression corpus.** `game/stage0/tests/corpus/` holds 20 recorded bot fights (novice, regular and expert, fixed scene seeds), each with its input frames and outcome (winner, cause, length, hits, damage taken, final state hash). `stage0_tests` replays every one on the current build, so an accidental change to a damage value, a timing or the sim's determinism turns it red. After changing a combat knob on purpose, re-record with `bin/regen-stage0-corpus` and commit the changed `.replay` files with the knob change; the diff shows which fights moved.

**The distribution is the diagnosis, not the totals.**

## 5. Kill criteria

Decided now, so the decision is not made later by sunk cost.

| Symptom | Read as | Action |
|---|---|---|
| Deaths mostly `unreadable` / `offscreen` | Telegraph problem | Fix wind-ups and audio, retest. **Design not implicated** |
| Deaths mostly `stamina` | Economy problem | Retune costs and regen |
| High time-to-retry; testers stop | Lethality too high | Raise TTK; reconsider i-frames |
| Fights feel safe or boring | Not lethal enough | Lower TTK before adding complexity |
| **Readable, fair, still not tense** | **Design problem** | **Stop.** Revisit commitment, TTK, or the identity itself |

Only the last row invalidates the design. Conflating it with the others is how a good idea
gets abandoned for the wrong reason.

**If the last row is reached**, the cascade is real: TTK changes flow into progression
curves, loot ratios, enemy density and world size — docs 05, 06, 07 and 11 all move. That
is acceptable at this stage and catastrophic later, which is why the gate is here.

## 6. Sessions

Append-only. One variable per session; never tune mid-session.

Generate the rows from the recordings with `stage0_report fights.csv --table`. It reads `fights.sessions.csv` next to the
CSV and prints one row per session: variable changed, fights, deaths by cause, mean time to kill (mean length of the fights
the player lost) and the diagnosis. Fights 1 to 20 are not counted.

| # | Date | Variable changed | Fights | Deaths by cause | Notes |
|---|---|---|---|---|---|
| — | — | *(none yet)* | — | — | — |

## 7. Outside testers

Two people, thirty minutes each, no help, watched. Recorded before they play, so the
learning curve is captured honestly.

| Tester | Date | Deaths | Retried immediately? | Quit at |
|---|---|---|---|---|
| — | — | — | — | — |

## 8. Verdict

*To be written at the gate, before any interpretation of what it "really" means.*

- [ ] Criterion 1 — a single fight is tense
- [ ] Criterion 2 — death causes point at design, not telegraphs
- [ ] Criterion 3 — outside testers retry rather than quit

**Outcome:** *(pending)*
