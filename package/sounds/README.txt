NorseSound rules
================

Every *.ini here swaps the game's sounds or chooses its music. Each [section] is one rule (its name is only a label);
rules run in load order, and nothing in the game's files changes.

Swap a sound everywhere Norse holds it (audio events, sound sets, animation sound notifies, unit effects, mission
activities):

    [Heavier hits]
    Swap = /Game/Norse/Audio/SoundFX/Old.Old -> /Game/MyPack/Audio/New.New     ; repeatable
    Load = /Game/...                          ; an asset to load first, when the holder is not loaded yet

Music when something happens (NorseEvents):

    [Fight music]
    On = FightStarted        ; FightStarted, FightEnded, MapLoaded, UnitDied, UnitDamaged, Payload:<name>
    When = Map(Borre)        ; optional, NorseConditions - about the event's unit
    Music = /Game/Norse/Audio/Music/Battle/S_Music_Battle_Even.S_Music_Battle_Even
    Volume = 1
    Stop = true              ; stop the music
    State = 2                ; the music arrangement's state
    Stinger = 0              ; a stinger variant
    Chance = 100

Norse's music is under /Game/Norse/Audio/Music/ (Battle, Exploration, Village, ...). A sound played on a hit or a
death belongs in NorseImpact (Sound = in impacts\). Console: snd.music (what is playing now), snd.test <rule>,
snd.apply (swap again), snd.reload.
