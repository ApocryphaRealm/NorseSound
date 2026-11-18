# Changelog

## 0.1.0 - 2026-11-18 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Swap = old -> new: a sound replaced wherever the loaded sound holders refer to it; the new sound is kept loaded.
- Music rules on NorseEvents moments and MapLoaded, with NorseConditions: Music, Volume, Stop, State, Stinger, Chance.
- Console snd.music, snd.test, snd.apply, snd.reload.

### Known (2026-11-18)
- A swapped-in sound can be freed at a map change (the root set does not hold it); Hit passed only because another rule also uses that sound. Fix pending: re-check written references at each map load (to-do list).
