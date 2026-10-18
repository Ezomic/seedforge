# Axiom coverage matrix

Generated from `data/axioms` and `data/synergies`. Do not edit by hand: regenerate with
`ichor_lint --coverage-out docs/axiom-coverage.md` (a test fails when this file is out of date).

## Summary

| | Pairs |
|---|---|
| Flagship synergy (content tier 1) | 11 |
| Text-only synergy (content tier 2) | 5 |
| No synergy, can roll together | 173 |
| Excluded (same slot or declared conflict) | 42 |
| Total | 231 |

Axioms: 22. Triple synergies: 1.

## Per axiom

| # | Axiom | Slot | Flagship | Text only | None | Excluded |
|---|---|---|---|---|---|---|
| 1 | `cataclysm.artemis_withdrew` | cataclysm | 2 | 1 | 13 | 5 |
| 2 | `cataclysm.demeters_grief` | cataclysm | 1 | 0 | 15 | 5 |
| 3 | `cataclysm.flood` | cataclysm | 2 | 0 | 14 | 5 |
| 4 | `cataclysm.kronos` | cataclysm | 1 | 0 | 15 | 5 |
| 5 | `cataclysm.silence_of_the_oracles` | cataclysm | 2 | 1 | 13 | 5 |
| 6 | `cataclysm.unbinding` | cataclysm | 1 | 0 | 15 | 5 |
| 7 | `law.hecatomb` | law | 1 | 0 | 15 | 5 |
| 8 | `law.last_favour` | law | 2 | 0 | 14 | 5 |
| 9 | `law.mysteries` | law | 2 | 1 | 13 | 5 |
| 10 | `law.oath` | law | 1 | 0 | 15 | 5 |
| 11 | `law.orichalcum` | law | 1 | 1 | 14 | 5 |
| 12 | `law.temenos` | law | 1 | 0 | 15 | 5 |
| 13 | `powers.amphictyony` | powers | 2 | 0 | 16 | 3 |
| 14 | `powers.hegemony` | powers | 0 | 0 | 18 | 3 |
| 15 | `powers.league` | powers | 2 | 0 | 16 | 3 |
| 16 | `powers.satrapy` | powers | 1 | 0 | 17 | 3 |
| 17 | `pressure.foretold` | pressure | 0 | 1 | 18 | 2 |
| 18 | `pressure.miasma` | pressure | 0 | 1 | 18 | 2 |
| 19 | `pressure.unburied` | pressure | 0 | 2 | 17 | 2 |
| 20 | `anomaly.labyrinth` | anomaly | 0 | 0 | 19 | 2 |
| 21 | `anomaly.tiresias` | anomaly | 0 | 1 | 18 | 2 |
| 22 | `anomaly.underworld` | anomaly | 0 | 1 | 18 | 2 |

## Pair matrix

`F` flagship, `T` text only, `.` none, `x` excluded, `-` the axiom itself. Numbers are the # column above.

| # | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | - | x | x | x | x | x | F | . | . | . | . | . | . | . | . | F | . | . | T | . | . | . |
| 2 | x | - | x | x | x | x | . | . | F | . | . | . | . | . | . | . | . | . | . | . | . | . |
| 3 | x | x | - | x | x | x | . | . | . | . | F | . | . | . | F | . | . | . | . | . | . | . |
| 4 | x | x | x | - | x | x | . | F | . | . | . | . | . | . | . | . | . | . | . | . | . | . |
| 5 | x | x | x | x | - | x | . | . | . | F | . | . | F | . | . | . | . | . | . | . | T | . |
| 6 | x | x | x | x | x | - | . | . | . | . | . | F | . | . | . | . | . | . | . | . | . | . |
| 7 | F | . | . | . | . | . | - | x | x | x | x | x | . | . | . | . | . | . | . | . | . | . |
| 8 | . | . | . | F | . | . | x | - | x | x | x | x | . | . | F | . | . | . | . | . | . | . |
| 9 | . | F | . | . | . | . | x | x | - | x | x | x | F | . | . | . | . | T | . | . | . | . |
| 10 | . | . | . | . | F | . | x | x | x | - | x | x | . | . | . | . | . | . | . | . | . | . |
| 11 | . | . | F | . | . | . | x | x | x | x | - | x | . | . | . | . | T | . | . | . | . | . |
| 12 | . | . | . | . | . | F | x | x | x | x | x | - | . | . | . | . | . | . | . | . | . | . |
| 13 | . | . | . | . | F | . | . | . | F | . | . | . | - | x | x | x | . | . | . | . | . | . |
| 14 | . | . | . | . | . | . | . | . | . | . | . | . | x | - | x | x | . | . | . | . | . | . |
| 15 | . | . | F | . | . | . | . | F | . | . | . | . | x | x | - | x | . | . | . | . | . | . |
| 16 | F | . | . | . | . | . | . | . | . | . | . | . | x | x | x | - | . | . | . | . | . | . |
| 17 | . | . | . | . | . | . | . | . | . | . | T | . | . | . | . | . | - | x | x | . | . | . |
| 18 | . | . | . | . | . | . | . | . | T | . | . | . | . | . | . | . | x | - | x | . | . | . |
| 19 | T | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | x | x | - | . | . | T |
| 20 | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | - | x | x |
| 21 | . | . | . | . | T | . | . | . | . | . | . | . | . | . | . | . | . | . | . | x | - | x |
| 22 | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | T | x | x | - |

## Pairs to author next

- `anomaly.labyrinth` + `cataclysm.artemis_withdrew`
- `anomaly.labyrinth` + `cataclysm.demeters_grief`
- `anomaly.labyrinth` + `cataclysm.flood`
- `anomaly.labyrinth` + `cataclysm.kronos`
- `anomaly.labyrinth` + `cataclysm.silence_of_the_oracles`
- `anomaly.labyrinth` + `cataclysm.unbinding`
- `anomaly.labyrinth` + `law.hecatomb`
- `anomaly.labyrinth` + `law.last_favour`
- `anomaly.labyrinth` + `law.mysteries`
- `anomaly.labyrinth` + `law.oath`
- `anomaly.labyrinth` + `law.orichalcum`
- `anomaly.labyrinth` + `law.temenos`
- `anomaly.labyrinth` + `powers.amphictyony`
- `anomaly.labyrinth` + `powers.hegemony`
- `anomaly.labyrinth` + `powers.league`
- `anomaly.labyrinth` + `powers.satrapy`
- `anomaly.labyrinth` + `pressure.foretold`
- `anomaly.labyrinth` + `pressure.miasma`
- `anomaly.labyrinth` + `pressure.unburied`
- `anomaly.tiresias` + `cataclysm.artemis_withdrew`
- `anomaly.tiresias` + `cataclysm.demeters_grief`
- `anomaly.tiresias` + `cataclysm.flood`
- `anomaly.tiresias` + `cataclysm.kronos`
- `anomaly.tiresias` + `cataclysm.unbinding`
- `anomaly.tiresias` + `law.hecatomb`
- `anomaly.tiresias` + `law.last_favour`
- `anomaly.tiresias` + `law.mysteries`
- `anomaly.tiresias` + `law.oath`
- `anomaly.tiresias` + `law.orichalcum`
- `anomaly.tiresias` + `law.temenos`
- `anomaly.tiresias` + `powers.amphictyony`
- `anomaly.tiresias` + `powers.hegemony`
- `anomaly.tiresias` + `powers.league`
- `anomaly.tiresias` + `powers.satrapy`
- `anomaly.tiresias` + `pressure.foretold`
- `anomaly.tiresias` + `pressure.miasma`
- `anomaly.tiresias` + `pressure.unburied`
- `anomaly.underworld` + `cataclysm.artemis_withdrew`
- `anomaly.underworld` + `cataclysm.demeters_grief`
- `anomaly.underworld` + `cataclysm.flood`
- `anomaly.underworld` + `cataclysm.kronos`
- `anomaly.underworld` + `cataclysm.silence_of_the_oracles`
- `anomaly.underworld` + `cataclysm.unbinding`
- `anomaly.underworld` + `law.hecatomb`
- `anomaly.underworld` + `law.last_favour`
- `anomaly.underworld` + `law.mysteries`
- `anomaly.underworld` + `law.oath`
- `anomaly.underworld` + `law.orichalcum`
- `anomaly.underworld` + `law.temenos`
- `anomaly.underworld` + `powers.amphictyony`
- `anomaly.underworld` + `powers.hegemony`
- `anomaly.underworld` + `powers.league`
- `anomaly.underworld` + `powers.satrapy`
- `anomaly.underworld` + `pressure.foretold`
- `anomaly.underworld` + `pressure.miasma`
- `cataclysm.artemis_withdrew` + `law.last_favour`
- `cataclysm.artemis_withdrew` + `law.mysteries`
- `cataclysm.artemis_withdrew` + `law.oath`
- `cataclysm.artemis_withdrew` + `law.orichalcum`
- `cataclysm.artemis_withdrew` + `law.temenos`
- `cataclysm.artemis_withdrew` + `powers.amphictyony`
- `cataclysm.artemis_withdrew` + `powers.hegemony`
- `cataclysm.artemis_withdrew` + `powers.league`
- `cataclysm.artemis_withdrew` + `pressure.foretold`
- `cataclysm.artemis_withdrew` + `pressure.miasma`
- `cataclysm.demeters_grief` + `law.hecatomb`
- `cataclysm.demeters_grief` + `law.last_favour`
- `cataclysm.demeters_grief` + `law.oath`
- `cataclysm.demeters_grief` + `law.orichalcum`
- `cataclysm.demeters_grief` + `law.temenos`
- `cataclysm.demeters_grief` + `powers.amphictyony`
- `cataclysm.demeters_grief` + `powers.hegemony`
- `cataclysm.demeters_grief` + `powers.league`
- `cataclysm.demeters_grief` + `powers.satrapy`
- `cataclysm.demeters_grief` + `pressure.foretold`
- `cataclysm.demeters_grief` + `pressure.miasma`
- `cataclysm.demeters_grief` + `pressure.unburied`
- `cataclysm.flood` + `law.hecatomb`
- `cataclysm.flood` + `law.last_favour`
- `cataclysm.flood` + `law.mysteries`
- `cataclysm.flood` + `law.oath`
- `cataclysm.flood` + `law.temenos`
- `cataclysm.flood` + `powers.amphictyony`
- `cataclysm.flood` + `powers.hegemony`
- `cataclysm.flood` + `powers.satrapy`
- `cataclysm.flood` + `pressure.foretold`
- `cataclysm.flood` + `pressure.miasma`
- `cataclysm.flood` + `pressure.unburied`
- `cataclysm.kronos` + `law.hecatomb`
- `cataclysm.kronos` + `law.mysteries`
- `cataclysm.kronos` + `law.oath`
- `cataclysm.kronos` + `law.orichalcum`
- `cataclysm.kronos` + `law.temenos`
- `cataclysm.kronos` + `powers.amphictyony`
- `cataclysm.kronos` + `powers.hegemony`
- `cataclysm.kronos` + `powers.league`
- `cataclysm.kronos` + `powers.satrapy`
- `cataclysm.kronos` + `pressure.foretold`
- `cataclysm.kronos` + `pressure.miasma`
- `cataclysm.kronos` + `pressure.unburied`
- `cataclysm.silence_of_the_oracles` + `law.hecatomb`
- `cataclysm.silence_of_the_oracles` + `law.last_favour`
- `cataclysm.silence_of_the_oracles` + `law.mysteries`
- `cataclysm.silence_of_the_oracles` + `law.orichalcum`
- `cataclysm.silence_of_the_oracles` + `law.temenos`
- `cataclysm.silence_of_the_oracles` + `powers.hegemony`
- `cataclysm.silence_of_the_oracles` + `powers.league`
- `cataclysm.silence_of_the_oracles` + `powers.satrapy`
- `cataclysm.silence_of_the_oracles` + `pressure.foretold`
- `cataclysm.silence_of_the_oracles` + `pressure.miasma`
- `cataclysm.silence_of_the_oracles` + `pressure.unburied`
- `cataclysm.unbinding` + `law.hecatomb`
- `cataclysm.unbinding` + `law.last_favour`
- `cataclysm.unbinding` + `law.mysteries`
- `cataclysm.unbinding` + `law.oath`
- `cataclysm.unbinding` + `law.orichalcum`
- `cataclysm.unbinding` + `powers.amphictyony`
- `cataclysm.unbinding` + `powers.hegemony`
- `cataclysm.unbinding` + `powers.league`
- `cataclysm.unbinding` + `powers.satrapy`
- `cataclysm.unbinding` + `pressure.foretold`
- `cataclysm.unbinding` + `pressure.miasma`
- `cataclysm.unbinding` + `pressure.unburied`
- `law.hecatomb` + `powers.amphictyony`
- `law.hecatomb` + `powers.hegemony`
- `law.hecatomb` + `powers.league`
- `law.hecatomb` + `powers.satrapy`
- `law.hecatomb` + `pressure.foretold`
- `law.hecatomb` + `pressure.miasma`
- `law.hecatomb` + `pressure.unburied`
- `law.last_favour` + `powers.amphictyony`
- `law.last_favour` + `powers.hegemony`
- `law.last_favour` + `powers.satrapy`
- `law.last_favour` + `pressure.foretold`
- `law.last_favour` + `pressure.miasma`
- `law.last_favour` + `pressure.unburied`
- `law.mysteries` + `powers.hegemony`
- `law.mysteries` + `powers.league`
- `law.mysteries` + `powers.satrapy`
- `law.mysteries` + `pressure.foretold`
- `law.mysteries` + `pressure.unburied`
- `law.oath` + `powers.amphictyony`
- `law.oath` + `powers.hegemony`
- `law.oath` + `powers.league`
- `law.oath` + `powers.satrapy`
- `law.oath` + `pressure.foretold`
- `law.oath` + `pressure.miasma`
- `law.oath` + `pressure.unburied`
- `law.orichalcum` + `powers.amphictyony`
- `law.orichalcum` + `powers.hegemony`
- `law.orichalcum` + `powers.league`
- `law.orichalcum` + `powers.satrapy`
- `law.orichalcum` + `pressure.miasma`
- `law.orichalcum` + `pressure.unburied`
- `law.temenos` + `powers.amphictyony`
- `law.temenos` + `powers.hegemony`
- `law.temenos` + `powers.league`
- `law.temenos` + `powers.satrapy`
- `law.temenos` + `pressure.foretold`
- `law.temenos` + `pressure.miasma`
- `law.temenos` + `pressure.unburied`
- `powers.amphictyony` + `pressure.foretold`
- `powers.amphictyony` + `pressure.miasma`
- `powers.amphictyony` + `pressure.unburied`
- `powers.hegemony` + `pressure.foretold`
- `powers.hegemony` + `pressure.miasma`
- `powers.hegemony` + `pressure.unburied`
- `powers.league` + `pressure.foretold`
- `powers.league` + `pressure.miasma`
- `powers.league` + `pressure.unburied`
- `powers.satrapy` + `pressure.foretold`
- `powers.satrapy` + `pressure.miasma`
- `powers.satrapy` + `pressure.unburied`

## Triples

- `flood_orichalcum_league` (flagship): `cataclysm.flood` + `law.orichalcum` + `powers.league`
