# Rhythm Level Generator (Geode, GD 2.2091) — beta

**Turn any song into a beat-synced Geometry Dash level in one click, right in the editor.**

The mod analyses the music (tempo, beats, downbeats, drops, breakdowns, long notes) and builds
a playable layout from it: cube, ship, wave, ball and spider parts, speed changes planned around
the song, and a full trigger / shader show that stays out of the way when the gameplay gets hard.
The generation itself is pure algorithms, with no AI model inside the mod.

## Download (beta)
1. Download `astray.rhythm-generator.geode` from the
   [latest release](https://github.com/TheAstrayDev/gd-rhythm-generator/releases).
2. Put it into `Geometry Dash/geode/mods` (Geode 5.11.2, GD 2.2091) and restart the game.
3. Open any level in the editor and press **AutoGen** at the top.

It's a beta: physics values for some orbs and pads are approximations, so play the result
before you upload it. Bug reports are welcome in [Issues](https://github.com/TheAstrayDev/gd-rhythm-generator/issues).

## How to use
1. In the editor, press **AutoGen** at the top centre.
2. **Choose Song** opens GD's own song menu: song ID field, Newgrounds search, the 2.2 Music
   Library and NCS. The song downloads automatically. With no song chosen, the level's main
   track is used.
3. Pick a difficulty (Easy … Demon), a seed (**New**), the sync offset (**−/+**, 10 ms steps,
   plus = later) and the options:
   - **Clear old gen** removes the previous generation: every object of the types the
     generator places;
   - **Effects** turns the triggers on or off.
4. Press **Generate**. The status line at the bottom says when it is done and whether the
   level was synced on GD's own timeline.

## How the generation works (`src/gen/`)
Every gamemode uses the same idea: first the player's path is built from the music, then the
obstacles are placed around that path. Each part is chosen by a search: a pool of random
candidates, then annealing cycles that improve the winner. The score is "spectacle": variety of
techniques, hitting the strong beats, no repetition. Options where the physics can't honour a
click are penalised.

- **Cube** (`Cube.cpp`):
  - phrase rhythms follow AAAB / AABB / ABAB / ABAC schemes built from the song's real onsets;
  - 24 candidates and 60 improvement steps for every bar;
  - jumps, yellow / pink / red orbs and pads, dash orbs on long notes;
  - plateaus up to 4 blocks, ledge drops, stairs, pillars;
  - switching to a ceiling plane with a blue orb or pad (Harder and up); upside down there are
    only plain jumps and a pause after landing;
  - techniques the level has already used a lot get penalised.
- **Ball / Spider** (`Flip.cpp`):
  - flips by click or by a gravity portal;
  - portals only on weaker beats, with calm before and after and no hazards around them;
  - shifting terrain: while the player is on one surface, the other one moves by a block;
  - hazard styles: spike row, spike clusters, block bumps, short row.
- **Wave** (`Wave.cpp`):
  - 18 rhythm templates per bar: beats, eighths, offbeats, gallop, clave, syncopation, bursts,
    "slide and rush", echo, follow the onsets and more;
  - per-bar modifiers: normal, tight, wide or "teeth" corridor, mini size, gravity;
  - the path comes from exact physics: the wave never flies flat and never runs into the ceiling.
- **Ship** (`Ship.cpp`):
  - 18 flight templates: waves, zigzag, terraces, climb, dive, straight fly, M, sawtooth, gates,
    squeeze, open air, offbeat dodge, echo and more;
  - every template has its own corridor shape; size and gravity change per bar.
- **Structure and speed** (`Plan.cpp`):
  - in loud parts the gamemode changes every 2–4 bars; mirror and gravity on drops;
  - the speed is planned from the song structure: drops and peaks one step faster, the
    breakdown after an intense part one step slower, everything else back at the base speed;
  - two-pass generation for exact sync: the mod first places only the speed portals, asks GD
    itself where the player is at every moment (`LevelEditorLayer::posForTime`), then builds
    all gameplay and effects on that table.

**Effects** (`Effects.cpp`): the whole trigger and shader toolbox except colour grading. The
level palette and block colours never change.
- **Obstacles** hide off-screen and fly into place on an eighth note right in front of the
  player; whatever the player has passed collapses down behind them.
- **Shaders:** shockwave and shockline on drops, an invert flash on the biggest drop, radial
  blur and zoom on build-ups, chromatic aberration on snares, bulge and pinch on downbeats,
  pixelate / glitch / chromatic glitch on gamemode changes, a vignette at the start, at the end
  and in calm parts.
- **Camera and flashes:** tilts and zoom punches, white flashes on kicks and snares, "breathing"
  corridor walls.
- **Busyness map:** before every screen-wide effect the generator checks how much the player has
  to read right then. Calm: full strength. Busy: a lighter version. Very busy: no effect.

Object IDs and property keys are cross-checked with the
[gmdbuilder](https://github.com/LXtreme/gmdbuilder) tables.

## Building
GitHub Actions (`.github/workflows/build.yml`) builds the mod from the `v6` branch of the Geode
SDK (GD 2.2091) and links it against the Geode 5.11.2 binaries.

Offline test of the analyser and the generator (no GD needed), including corridor and trap checks:
```bash
python -m ziglang c++ -std=c++20 -O2 -D_USE_MATH_DEFINES test/offline_test.cpp src/Analysis.cpp src/Generator.cpp src/gen/*.cpp -o offline_test.exe
```

## Limitations
- Player physics are approximations with safety margins: orb and pad strengths, ball gravity,
  the push of blue orbs, hitboxes. Play the level before you publish it.
- GD's fonts have no Cyrillic, so the mod's interface is in English.

## Credits
Built with [Claude](https://claude.ai) (Anthropic): the analysis, the generator and the Geode
UI were written with Claude, then tuned through a lot of playtesting.
