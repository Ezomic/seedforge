# 18 — Vertical Slice

**Milestone 1.** A hand-built 500 m square with no procedural content.

> The slice's job is to **answer questions**, not to be a small version of the game.

---

## What it must answer

Scope around the **riskiest** assumptions, not the most enjoyable ones to build.

| Assumption | Risk | Cost if wrong late |
|---|---|---|
| Rare + lethal + low-TTK combat is **tense**, not frustrating | **Highest** | Cascades into progression, loot ratios, enemy density, world size |
| Scarcity is engaging, not tedious | High | Rewrites the economy and loot ratios |
| No-waypoint landmark navigation works | Medium | Changes UI, art requirements, generator validation |
| The Law-swap is one system, not six | Medium | Architectural rework across combat, HUD, economy |

> **World generation is deliberately absent.** It is laborious, not risky — well-understood
> engineering. It does not belong in a slice whose purpose is de-risking.

---

# Stage 0 — the grey-box prototype

**About four weeks. One empty box, one enemy, no art, no map, no loot.**

Stage 0 answers the highest-risk question at roughly **10% of the slice's cost**. If a single
fight is not tense in a grey box, it will not become tense because a forest was added around
it.

> The ticketed estimate is 164 h across 14 tickets. An earlier draft said "two to three
> weeks", which was wishful — building a controller, a combat kit, an AI, instrumentation
> *and* running 200 tuning fights does not fit in three weeks for one person. The four-week
> figure is the honest one; the scope is unchanged.

> The live record — hypothesis, parameters, tuning sessions, death-cause data and the
> final verdict — is kept in **[stage0-log.md](stage0-log.md)**.

## Write the hypothesis first

Stage 0 fails most often by quietly becoming a *combat system* instead of an *experiment*.
The difference is that an experiment has a falsifiable hypothesis, a measurement, and a kill
condition.

> A fight of 3–8 seconds against one ordinary enemy, where either party dies in 2–3 hits and
> attacks commit with no cancel, produces **tension** rather than frustration — **provided
> every wind-up is readable.**

That last clause names the confound. If it feels bad, the cause is either the design or the
telegraphs, and those need opposite fixes. Projects routinely conclude "lethal combat isn't
fun" when what they actually built was *unreadable* combat.

## Grey-box the art, not the timing

The one thing that cannot be placeholdered is **animation timing**. Commitment-based combat
*is* its frame data — wind-up, active, recovery. Evaluating it with animations that snap
instantly measures nothing.

- Untextured geometry, one grey material, flat lighting, capsules and boxes
- **Real wind-ups, real recovery, real root motion.** Buy a basic melee set or hand-key
  blockouts with deliberate frame counts
- **Three distinct wind-up sounds.** Not polish — half the readability variable

## What to build

**Player.** Walk, run, spear and shield (spacing reads more legibly with reach than with a
sword, and it fits the setting). Light attack, heavy attack. Block. Directional dodge-step.
Stamina. Health.

**One skirmisher.** Specified in full below — it *is* the experiment. Resist making it
clever; if a dumb enemy at correct spacing is tense, that is a real result.

Nothing else. No magic, no second enemy, no combos, no parry, no second weapon.

## Starting numbers

Tune from these rather than guessing.

| | Value |
|---|---|
| Player / enemy HP | 100 |
| Light / heavy damage | 30 / 50 |
| Wind-up, light / heavy | 0.35 s / 0.7 s |
| Recovery, light / heavy | 0.4 s / 0.8 s |
| Stamina pool | 100 |
| Cost: light / heavy / dodge | 20 / 35 / 25 |
| Stamina regen | 25/s after 1 s idle |
| Block | absorbs damage as stamina at 40% |

Three hits either way; a fight is about four exchanges; running dry on stamina should be
survivable exactly once.

## The riskiest single decision

> **Dodge has no invincibility frames.** It is a committed movement burst, not a phase-through.

Almost every action game leans on i-frames. Removing them makes **spacing** the entire skill,
which is the point and what makes terrain matter later. It is also the call most likely to be
wrong: if the prototype reads as unfair while the telegraphs are demonstrably clean, this is
suspect number one.

---

## The skirmisher state machine

The player's controls only need to stay out of the way. **The enemy is what creates the
fight**, so this is the most load-bearing single piece of Stage 0.

### It is a rhythm generator, not an opponent

Its job is not to be smart or to win. It is to produce **readable pressure with legible
gaps** — the player continuously choosing under threat, able to see openings and take or
decline them.

The governing variable is the **window**: the beat after a commit where the player decides to
punish, reposition, drink, or wait. Too short and the fight is reaction-only; too long and it
is trivial; unreadable and it is frustrating at any length.

### States

| State | Purpose |
|---|---|
| `Unaware` | Pre-aggro. Minimal now; matters once the bow exists |
| `Approach` | Close to the engagement band |
| `Neutral` | Strafe, hold spacing, accumulate desire. **The most important state** |
| `Tell` | ~0.15 s weight-shift before wind-up |
| `Commit` | Wind-up → active → recovery |
| `Punish` | Fast reactive attack on a player whiff |
| `Rest` | Deliberate back-off |
| `Stagger` / `Dead` | Poise broken; terminal |

`Neutral` is routinely under-built. Tension lives there — an enemy that is always attacking
has no neutral, and a fight with no neutral contains no decisions.

### Committing: a desire accumulator, not a timer

A timer is predictable and dull; pure reaction feels arbitrary. Instead, desire builds while
in the engagement band — faster when the player is close, faster still when the player is in
their own recovery. Commit when `desire > threshold`, then reset with jitter.

Pressure becomes something the player can feel building without being able to count it.

### The rest cadence is designed content

After three commits or ~6 seconds of pressure, the skirmisher **must** disengage for 1–2
seconds.

> The pause is a feature, not an absence of behaviour. It is where the player heals,
> repositions and thinks, and it is what makes a fight read as an exchange rather than an
> onslaught. **Continuous pressure removes decision-making and reads as unfair even when it
> is survivable.**

### Anti-frustration rules

**Clamp rotation during commit — 30°/s during wind-up, zero during active frames.**

> This is the most important rule on the page. Enemies that snap-track through their own
> attack are the commonest reason commitment-based combat feels cheap: the dodge is correct,
> the hit lands anyway, and the player correctly concludes the game is lying to them.
>
> **With i-frames removed, this clamp is doing all the fairness work i-frames would otherwise
> do.** Get it wrong and Stage 0 will read as a design failure when it is a tracking bug.

Also:

- **Never attack from off-screen without audio** — see the telegraph contract
- **No chaining into player stagger** beyond a single follow-up. No true loops
- **Must return to spacing before re-committing**, guaranteeing a beat between attacks

### Readability from the AI side

- **The `Tell` state.** 0.15 s of weight-shift before the wind-up proper. Enormous
  readability gain without lengthening the attack — the cheapest fairness upgrade available
- **Wind-up silhouettes must differ between attacks**, not just durations. If light and heavy
  look alike for their first 0.3 s the player is guessing, and guessing reads as unfair

### Give the enemy stamina

It should tire visibly: slower strafing, longer rests, heavier breathing. Three things follow —
defensive play gains a **readable reward**, the rest cadence becomes diegetic rather than
arbitrary, and the player has a reason to watch the enemy instead of their own cooldowns.

### The knobs are the tuning protocol's variables

Six floats, editable without recompiling. If tuning needs a code change it will happen less
often and teach less.

```
desireGainRate        preferredSpacing
restDuration          punishReactionTime
windupScale           trackingCapDegPerSec
```

### It emits the telemetry the gate depends on

The machine knows the answer at the moment of the killing blow, so have it stamp the context
rather than filling the taxonomy in by hand:

> attacker on screen · player mid-recovery · player stamina at impact · which attack ·
> time since the player's last input

That is the difference between *"it felt unfair"* and *"41% of deaths were off-screen
attacks."*

### Implementation

**A C++ enum and a switch statement on the pawn. Not a Behaviour Tree.** BTs earn their keep
with many agents and designer-authored logic; for one enemy in disposable code a switch is
faster to write, faster to change and trivially debuggable. Blackboards and task nodes are
ceremony for something being thrown away.

**Always-on debug draw**: state, desire, spacing, active timers, tracking angle. You will read
that overlay more than the code.

Perception is `distance + dot(forward, toPlayer)` — no perception system, no stimuli config.

### Starting values

| | |
|---|---|
| Preferred spacing | 2.5 m (just outside spear reach) |
| Desire threshold | 1.0; +0.35/s in band, +0.6/s if player recovering |
| Rest trigger | 3 commits or 6 s |
| Rest duration | 1.5 s |
| Tell | 0.15 s |
| Tracking cap | 30°/s wind-up, 0°/s active |
| Punish reaction | 0.25 s after whiff detection |
| Enemy stamina | 100; 30 per attack; regen 15/s |

### Not in the skirmisher

Group coordination · flanking · pathfinding beyond `MoveTo` · a perception system · difficulty
scaling · more than two attacks · **feints** (tempting, and only meaningful once the player can
reliably read the honest attacks).

---

## Instrumentation

"Tense or frustrating?" cannot be answered by recollection after 200 fights.

Log every fight: duration, HP remaining, damage taken, hits landed, whiffs, blocks, dodges.
Then the part that does the real work — **classify every death by cause**, which the
[state machine stamps automatically](#it-emits-the-telemetry-the-gate-depends-on) rather than
being recorded by hand:

- ran out of stamina
- whiff-punished
- cornered / no room
- mistimed block
- attack came from off-screen
- **couldn't read the telegraph**

**The distribution is the diagnosis.** Deaths dominated by "couldn't read it" means fix the
telegraphs and retest — that is not a verdict on the design.

Also log **time-to-retry**. Fast re-engagement is what tension looks like in telemetry;
hesitation before restarting is what frustration looks like.

## The 200-fight protocol

Not "play a lot."

- **Fights 1–20:** does it function at all. Expect bug-fixing, not tuning
- **Then one variable per session.** Change wind-up timing, play 30 fights, log, stop
- **Never tune mid-session.** Feel is comparative and memory of the previous build is short

## Outsiders at the end of Stage 0

By fight 200 you are an expert at a game nobody else has played and physically cannot feel
what a newcomer feels.

1. **Record the first 20 fights.** The only honest record of what learning it looks like
2. **Two outsiders, thirty minutes each, no help, watched**

> This is deliberately earlier than a slice-end playtest. If the core is frustrating, finding
> out three months and a dungeon later is the expensive version of the same discovery.

## Stage 0 code is disposable

**Stage 0 ships nothing and survives nothing.** It is an experiment whose output is an
answer, not a foundation.

Do **not** apply the architecture constraints here — no listen-server setup, no event
sourcing, no Gameplay Ability System. Architecting a throwaway delays the only thing you need
to know, and GAS in particular is the right question for the slice (when abilities exist) and
the wrong thing to learn while finding out whether the fight is fun.

If Stage 0 passes, the loop is rebuilt properly with architecture in place — days, not weeks,
because by then you know exactly what you are building.

## Kill criteria — symptom to diagnosis

| Symptom | Read as | Action |
|---|---|---|
| Deaths mostly "couldn't read it" | Telegraph problem | Fix wind-ups and audio, retest. Design not implicated |
| Deaths mostly "out of stamina" | Economy problem | Retune costs and regen |
| High time-to-retry; testers stop | Lethality too high | Raise TTK; reconsider i-frames |
| Fights feel safe or boring | Not lethal enough | Lower TTK before adding complexity |
| **Readable, fair, still not tense** | **Design problem** | The real kill condition — revisit commitment, TTK, or the identity |

Only the last row invalidates the design. Conflating it with the others is how projects
abandon a good idea for the wrong reason.

## Scope guards

Week two will suggest every one of these. Write them down and refuse them:

Second enemy type · a combo · a second weapon · making the box look nicer · starting the
500 m map · adopting GAS · a parry · levelling.

---

# The slice proper

Begins only after the Stage 0 gate passes.

## Architecture, before anything permanent

These are the first work of the slice, **not** of Stage 0 — they cannot be retrofitted into
what you intend to keep, but they must not slow the experiment that decides whether there is
anything to keep. See [15](15-multiplayer.md#what-must-be-decided-now-regardless).

1. **Run single-player as a one-player listen server.** The most important item here
2. **Mutate state only through an event/command layer**
3. **No singleton assumptions about "the player"** — audio, streaming, spawning
4. **The one-page style guide**, including the 2 m or 4 m snap grid
   ([16](16-art-direction.md#the-style-guide--week-one), [09](09-dungeons.md#seams))

## The 500 m map is a test fixture

Designed deliberately to contain the five terrain conditions that will break AI once the
generator exists:

1. **Open field** · 2. **Dense forest** · 3. **A narrow defile** · 4. **A steep slope** ·
5. **Shallow water and a bank**

Plus a **ridge** for a vantage point, a **river** for navigation-by-water, and **two or three
landmark silhouettes**.

> **Every enemy is validated in all five conditions.** Steep slopes and water edges are where
> UE navmesh and root-motion attacks reliably fall apart. A hand-built level would avoid them;
> this one must not, because
> [the generator will produce them constantly](05-combat.md#enemies-roles-not-species).

**Content on the map:** one small settlement (3–4 buildings — merchant, trainer, stash, one
NPC), one regional-tier dungeon of ~15 minutes, and a handful of minor sites.

## Scope: in

### Combat
- Add a **bow** to the Stage 0 melee kit. Melee-only tests half the identity: the
  preparation-forward loop is "open at range, finish in melee"
- **Anchor** (tests the stagger economy) and **Artillery** (tests positioning and terrain)
- Skip Swarm, Controller and Ambusher — they need crowd tech, status effects and perception
  tuning respectively, and none tests anything the first three do not

### Loot & economy
- Consumables, charges, 4–5 catalogue items, **encumbrance on** — not optional, since
  "is scarcity fun or annoying" is a primary question
- One merchant, one repair service

### Progression
- Discovery XP, the four stats, one trainer with 4–5 capability unlocks
- No respec, no faction standing

### Navigation
- **No waypoints, no minimap.** Compass, verbal directions, landmarks
- A functional but ugly map screen — the hand-drawn one is an art investment, deferred

### Art
- Megascans, one bought architecture kit, one bought animation set. **Make nothing custom**
- **Do** establish the material-response clamp and one colour grade
- **Covered faces from day one**

## The Law axiom, and the architecture test

Build the ability system with a **swappable power source**. Ship the slice on **Hecatomb**:
health *is* the offering, one bar, no extra HUD, and it directly stress-tests scarcity
because using power spends survivability.

Near the end, implement **The Last Favour** (finite charges) as a second configuration.

> **If swapping Hecatomb → The Last Favour takes more than a day, the abstraction is wrong**
> and two combat systems have been designed by accident.

## Scope: out

No world generation · no axiom system (one Law hardcoded, one swap test) · no factions ·
no quest generation (one hand-written quest) · no systemic dialogue · no consequence ledger ·
no dungeon generation · no ambience or music · no crafting · no fast travel ·
no save system beyond "it saves".

---

## Success criteria — written before building

Otherwise whatever gets built will be rationalised into a pass.

**Stage 0 gate:**
1. One skirmisher, no consumables, full health — **is the fight tense?**
2. Does the death-cause distribution point at design rather than telegraphs?
3. **Do the two outside testers die and immediately try again?**

**Slice gate:**
4. Can a fresh player reach the dungeon from a spoken direction, with no waypoint?
5. After clearing the dungeon, does the player feel they **spent** something?
6. Does the Hecatomb → The Last Favour swap take under a day?
7. Does a fresh player die at least once and immediately want to try again?

Criteria 3 and 7 are the real tests, and the ones that cannot be answered from inside the
project. **Tension and frustration feel identical from within and completely different from
outside.**

## Build order

| # | Step | Notes |
|---|---|---|
| **Stage 0** | | |
| 1 | Controller, camera, one attack, health, damage | Ugly and playable by day 4 |
| 2 | Stamina, block, dodge, second attack, skirmisher, wind-up audio | Instrumentation lands here, not later |
| 3 | 200 fights, one variable per tuning session | |
| 4 | Two outside testers; **write the verdict down** | **GATE** |
| **Slice** | | |
| 5 | The four architecture items | First permanent code |
| 6 | **Rebuild the Stage 0 loop on it** | Stage 0 was disposable; this is where it becomes real |
| 7 | Bow and the preparation loop | |
| 8 | Anchor and Artillery, validated across all five terrain conditions | |
| 9 | The 500 m map | |
| 10 | Loot, consumables, encumbrance, merchant, repair | Scope creep starts here |
| 11 | The dungeon | |
| 12 | Discovery XP, trainer, capability unlocks | |
| 13 | Navigation, landmarks, the no-waypoint test | |
| 14 | The Law swap test | |
| 15 | Full outside playtest, five fresh players | |

Steps 1–4 are **a fifth of the hours and the majority of the risk**. Everything after step 7
is confirmation and context; **step 3 is the actual experiment.**

Step 6 exists because Stage 0 is thrown away on purpose. It was previously implied by the
text and had no ticket, which is exactly how a week of work goes missing from a plan.

## Estimate

| | Ticketed | |
|---|---|---|
| Stage 0 | 162 h | ~4 weeks |
| Slice proper | 642 h | ~16 weeks (3.7 months) |
| **Milestone 1 total** | **804 h** | **~20 weeks (4.6 months)** |

Both figures were recalibrated after the first coarse pass, which had inflated Stage 0 to
388 h and priced slice tickets as though each were built from scratch. They are not: **Stage 0
leaves reusable pieces behind.** Enemy roles reuse the skirmisher machine, the bow reuses the
weapon class, and merchant and trainer share one interaction pattern — so the second enemy
costs a fraction of the first.

Working assumptions: 40 h weeks, one person, reasonable UE C++ fluency, and no
already-solved-it advantage on any of it.

> Vertical slices routinely take twice their estimate, mostly from step 9 onward where "just
> one more system" accumulates. **The gate at step 4 is what protects against spending that
> before knowing whether the premise holds** — it is 162 h in, or a fifth of the milestone.

## Next

On passing: [17 — Order of work](17-tech-stack.md#order-of-work), step 2 — the NumPy
generator, which is engine-independent and can proceed in parallel.
