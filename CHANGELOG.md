# Changelog

## 0.1.0 - 2026-11-18 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Swap = old -> new: a sound replaced wherever the loaded sound holders refer to it; the new sound is kept loaded.
- Music rules on NorseEvents moments and MapLoaded, with NorseConditions: Music, Volume, Stop, State, Stinger, Chance.
- Console snd.music, snd.test, snd.apply, snd.reload.

### Known (2026-11-18)
- A swapped-in sound can be freed at a map change (the root set does not hold it); Hit passed only because another rule also uses that sound. Fix pending: re-check written references at each map load (to-do list).

### Changed (2026-11-18, refinement)
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.

### Fixed (2026-11-18, refinement)
- Conditions are compiled from the first map load on, not at start-up (they no longer depend on loading after NorseConditions).
