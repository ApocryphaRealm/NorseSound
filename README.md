# NorseSound

Swap the game's sounds and choose its music by rule, for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0).
Norse uses Unreal's own audio (MetaSounds and sound waves, no Wwise or FMOD). Sounds are held by AudioEvent assets
(`SoundToPlay`), SoundSets, sound notifies on animations, unit effects and mission activities; music is played by the
game's `MusicSubsystem`. Nothing in the game's files changes. Modelled on Sound Record Distributor (Skyrim). Survey:
`RESEARCH-more-frameworks.md` (#15).

```ini
[Punchier hits]
Swap = /Game/Norse/Audio/SoundFX/Combat/Hits/MS_Hit.MS_Hit -> /Game/Norse/Audio/SoundFX/Combat/Abilities/Punch/Ms_PunchAbility.Ms_PunchAbility

[Fight music]
On = FightStarted
When = Map(Borre)
Music = /Game/Norse/Audio/Music/Battle/S_Music_Battle_Winning.S_Music_Battle_Winning
```

- **Swap** `old -> new` replaces every reference to a sound in the loaded sound holders, at each map load and on
  `snd.apply`/`snd.reload`. The new sound is kept loaded (root set): a start-up asset's reference would not keep it
  alive.
- **Music rules**: `On` = FightStarted, FightEnded, MapLoaded (3 s after a map starts), UnitDied, UnitDamaged,
  Payload:<name>; `When` = a NorseConditions condition; actions `Music` (+ `Volume`), `Stop`, `State`, `Stinger`;
  `Chance`.
- A sound on a hit or a death is NorseImpact's (`Sound =` in its impacts).

Author guide: `package\sounds\README.txt`; tested rules: `docs\examples\`. Console: `snd.music` (what is playing),
`snd.test <rule>`, `snd.apply`, `snd.reload`; NorseDevTools reloads when a file in `sounds\` is saved.

0.1.0 tested in game (TEST-RESULT-0.1.0.txt). Not yet: hearing a swapped sound in a real hit, State and Stinger, a
real mission's music.
