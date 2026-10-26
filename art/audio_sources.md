# Audio sources (provisional ledger, SEED-682)

Every recorded clip in `art/audio/` is cut from a CC0 source on OpenGameArt. The licence on each page was read from the page itself (the "CC0" licence badge) on the retrieval date. This file stands in for the licence ledger of SEED-680 and moves into it when that lands.

Retrieved: 2026-10-26. Page URLs are `https://opengameart.org/content/<slug>`. "Uploader" is the OpenGameArt account that published the file; where the page credits the recordist separately, that name comes second.

Regenerate the clips with `python art/audio/make_clips.py SOURCES_DIR` (see the header of that script). The recipe for each clip (source file, start, length) is in `CLIPS` there. Every clip is mono 16-bit 32 kHz, a loop is cross-faded so its end meets its start, and its level is set by kind (steps and one-shots about -18 dBFS RMS, beds -26, hour layers -28).

Not recorded, on purpose: `A_Splash`, `A_Rain` and `A_Wind` are made in code (`wading_splash.cpp`, `rain_ambience.cpp`, `weather_ambience.cpp`), and `A_Tell`, `A_WindupLight` and `A_WindupHeavy` are synthesised by `art/audio/make_cues.py`. None of them is a placeholder.

| Clip | Source page (slug) | Uploader / recordist | Licence | Source file used |
|---|---|---|---|---|
| A_Step_Grass | fantozzis-footsteps-grasssand-stone | Raassh (uploaded by qubodup) | CC0 | Fantozzi-SandL1.flac |
| A_Step_Rock | fantozzis-footsteps-grasssand-stone | Raassh (uploaded by qubodup) | CC0 | Fantozzi-StoneL1.flac |
| A_Step_Road | fantozzis-footsteps-grasssand-stone | Raassh (uploaded by qubodup) | CC0 | Fantozzi-StoneR2.flac |
| A_Step_Snow | 42-snow-and-gravel-footsteps | Corsica_S, extracted by Iwan Gabovitch (uploaded by qubodup); the readme in the archive grants CC0 | CC0 | Corsica_S-Walking_on_snow_covered_gravel_01.flac |
| A_Bed_Surf | beach-ocean-waves | Four Bidden, from a Freesound CC0 recording by jasinski | CC0 | wave_01, wave_02, wave_03 (alkaibeach .flac) |
| A_Bed_Lake | sea-and-river-wave-sounds | RandomMind (uploaded by Snabisch) | CC0 | Vistula.mp3 from Vistula.zip, 1200 s in |
| A_Bed_Taiga | windy-ambient-noise | Some Weirdo | CC0 | windy_ambiance_0.ogg, 30 s in |
| A_Bed_Forest | amb-outside-1 | Kresiek The Furry | CC0 | amb_outdoor1_loop.wav |
| A_Bed_Grassland | mild-wind-background-noise | Bashar3A | CC0 | wind background noise 2.wav, 10 s in |
| A_Bed_Marsh | swamp-environment-audio | Jorge Avila (uploaded by LokiF) | CC0 | swamp.ogg, 40 s in |
| A_Bed_Rainforest | dripping-water-loop | supergamez2014 (uploaded by qubodup) | CC0 | atmosbasement.mp3_.flac |
| A_Bed_Savanna | cicada-sounds | syncopika | CC0 | 082526-cicadasounds_0.ogg, 5 s in |
| A_Bed_Blight | dark-ambiences | johndh (uploaded by Ogrebane) | CC0 | ambience-1.wav from dark_ambiences.zip |
| A_Bed_Farmland | amb-morning-sounds-perfect-loop | Kresiek The Furry | CC0 | amb_morning.wav, 10 s in |
| A_Dawn_Birds | park-ambiences | Thimras | CC0 | park_ambience_birds.wav, 60 s in |
| A_Day_Insects | cicada-sounds | syncopika | CC0 | 082526-cicadasounds2_0.ogg, 10 s in |
| A_Dusk_Crickets | crickets-ambient-noise-loopable | 2DPIXX (uploaded by Wolfgang_) | CC0 | crickets_1.mp3 |
| A_Night_Insects | birdcricketfrog-and-mosquito-sounds | Aj_ | CC0 | cricket2.wav from birdsCrickets.zip |
| A_Night_Wolves | wolf-monster-sound | CaveboyTup | CC0 | wolf_monster_6.mp3 |
| A_Settlement_Chatter | crowd-shoutingspeaking-ambience | StarNinjas | CC0 | crowd_shouting_0.ogg, 5 s in |
| A_Settlement_Bell | bell-dingschimes | PWL | CC0 | bell_ding3.wav |
| A_Settlement_Mill | park-ambiences | Thimras | CC0 | park_ambience_river.wav, 100 s in (running water, standing in for a mill race) |
| A_Settlement_Surf | beach-ocean-waves and solo-seagull-sound-effects | Four Bidden / jasinski; Rango Mango | CC0 | the A_Bed_Surf waves mixed with Seagull Ambient 1.wav |
| A_Settlement_Machinery | steam-boiler-sound-loop | bart | CC0 | generator_loop.wav, 4 s in |

## Known weak spots

- `A_Settlement_Mill` is river water, not a turning wheel; no CC0 mill recording turned up. `A_Settlement_Bell` is a hand bell ding, not a tower bell.
- `A_Night_Wolves` is a short creature growl named "wolf monster", not a howl. `A_Night_Insects` is a 3 s cricket loop that repeats quickly.
- `A_Step_Road` is a second stone step and sounds close to `A_Step_Rock`.
- Not listened to by a person in this change: levels, seams and format are tested, how well each clip suits its cue is not.
