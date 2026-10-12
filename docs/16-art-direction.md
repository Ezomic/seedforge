# 16 — Art Direction

> **You will lose a fidelity contest and you can win an atmosphere contest.** Fidelity costs
> assets, animation, and tech art. Atmosphere costs fog, light, colour, and sound. Direct
> every decision through that.

## The unification problem

The world is assembled at runtime from kits, libraries, and scatter. Without art direction
doing active unification work, that reads as an asset flip regardless of how good the
individual pieces are.

**Coherence, not quality, is the thing to solve.**

## Style: photoreal base

Photoreal foundation with heavy atmospheric and grading treatment.

Not because it is the best aesthetic, but because the free and cheap asset ecosystem is
overwhelmingly photoreal, and Megascans material is internally consistent, enormous, and
imports as plain glTF and PNG.
Going stylised means making nearly everything by hand, which is a different project.

That is a budget-driven call, and budget-driven calls are legitimate. The trap: photoreal
invites AAA comparison. **Lean into atmosphere and mood**, where a small team can genuinely
out-compete, not detail density, where it cannot.

## The unification stack

Techniques that make mixed-source assets read as one world, roughly in value order:

**1. Terrain blending.** Rocks, buildings, and props blend into the terrain
material at their base.
> Highest-value item on the list. **Objects sitting visibly on top of terrain is the number
> one visual tell of procedural placement.** Fix it and half the "generated" feeling disappears.

**2. Unified material response.** Override roughness and specular ranges across every asset so
they share a lighting behaviour. Mixed-source assets usually look wrong not because of texture
quality but because their PBR values disagree. Clamp them and they belong together.

**3. A restricted palette per axiom**, as a global grade plus LUT. The biggest perceptual win
available, and nearly free.

**4. Atmospheric depth.** Exponential height fog and volumetric fog, tuned per biome. Distance
haze hides quality falloff, makes vistas read, and does most of the emotional work outdoors.

**5. One lighting model.** A single sun/sky setup parameterised per biome and time of day —
never per asset, never hand-tuned per region.

**6. A weathering pass.** The scatter pass spreads grime, moss, and damage decals uniformly. This is what
disguises kit seams and makes assembled architecture look weathered rather than snapped together.

## Silhouette discipline

Navigation depends on landmarks readable at a kilometre; combat depends on enemies readable
against foliage. Make this a formal review step:

> **Look at every hero asset as a pure black silhouette. If it is not identifiable, it is not
> done.**

Five seconds per asset, catches nearly everything, and it ties art directly to
[navigation](11-traversal-map.md#navigation-landmarks-over-waypoints) and
[combat readability](05-combat.md#readability-without-controlled-lighting).

## Axiom palettes

Where the axiom system becomes *visible* in the first three seconds of a new world.

| Cataclysm | Palette |
|---|---|
| **Deukalion's Flood** | Desaturated blue-green, salt white, verdigris on bronze. Waterlines on every column at a consistent height, everywhere |
| **Demeter's Grief** | Bone white, pale blue shadows, low thin sun, hard edges. Marble that reads as cold rather than noble |
| **Silence of the Oracles** | Gold gone grey. Dust, empty plinths, colonnades with nothing at the end of them |
| **The Unbinding** | Exposed strata as dominant texture, raw rock colour, dust plumes, deliberate vertigo |
| **Artemis Withdrew** | A completely normal palette in which **nothing moves.** No birds crossing the sky, no swaying wildlife, no distant motion. Horror entirely in the absence — and it costs nothing |
| **The Age of Kronos** | Warm, saturated, full. Painted marble as it actually was, not the bleached version — deliberately unfamiliar, so every other world feels wrong by comparison |

## Buy versus make

> **Make what players look at closely and remember; buy what they walk past.**

| Buy / free | Make |
|---|---|
| Nature and terrain materials | Landmark silhouettes |
| Base architecture kits | Axiom-specific hero assets |
| Animation sets | The ~60 named catalogue items |
| Generic VFX | Faction identity |

This works out well against the [loot design](06-loot-economy.md): gear is only 5% of loot
and the catalogue is finite, so the items players remember are a *makeable* number.

## Faction identity through materials, not meshes

Each faction gets a colour, a silhouette motif, and a material treatment, applied via
material parameters to shared meshes. One guard mesh becomes six factions.

Same design language as everywhere else in the project: **roles and instances, not bespoke
everything.**

## Characters, and the trick the fiction hands you

Photoreal *environments* are achievable with a small team because Megascans does the heavy
lifting. Photoreal *characters* are not — faces and animation are where small teams reliably
lose.

> **Cover the faces.** Helmets, hoods, masks, wraps, respirators, heavy weather gear, faction
> uniforms.

The setting justifies it completely, and Greek helmets are the strongest case available:
the Corinthian type covers the entire face by design. Demeter's Grief demands cover, the
Flood demands it, Artemis Withdrew makes it eerie, and a satrap's garrison wears helmets
anyway.

This single decision removes **facial rigging, facial animation, lipsync, and uncanny valley**
from the critical path. It is the biggest character-art saving available and the fiction pays
for it for free.

Reserve exposed faces for the [10–15 Principals](10-npcs.md#four-tiers), where the attention
is affordable.

## VFX per Law

Cheap identity, since VFX is parameterisable.

| Law | Look |
|---|---|
| Orichalcum | Hot metal, sparks, forge glow |
| Hecatomb | Red, wet, visceral, uncomfortably physical. Ichor as gold at the divine tier |
| The Mysteries | Torchlight, smoke, things glimpsed by initiates only |
| Temenos | Light anchored to the ground rather than the caster |
| The Last Favour | Guttering, failing, flickering out |
| The Oath | Shadow, tally marks, things briefly visible |

## The style guide — week one

One page, written before assets accumulate:

- Palette per axiom
- Material value ranges (roughness, specular)
- World scale rules — door heights, human reference
- The snap grid (2 m or 4 m — see [09](09-dungeons.md#seams))
- Silhouette requirements
- Fog and grade defaults

Every asset checked against it before entering the project.

> **Drift is what kills coherence in long projects, and drift is unnoticeable month to
> month.** A written standard is the only thing that catches it.
