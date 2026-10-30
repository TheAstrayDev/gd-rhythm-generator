# Rhythm Level Generator (Geode, GD 2.2091) — beta

**Turn any song into a beat-synced Geometry Dash level in one click, right in the editor.**

The mod analyses the music (tempo, beats, downbeats, drops, breakdowns, long notes, and the
vocals as a separate pool) and builds a playable layout from it: cube, ship, wave, ball, spider,
robot, UFO and swing parts, dual portals, teleports, speed changes planned around the song, a
design layer learned from RobTop's own levels, and a full trigger / shader show that stays out
of the way when the gameplay gets hard. Every release is checked by a playthrough test: the path
the generator intends never touches a block, a slope, a spike or a saw.
Pick the styles you like and every level blends them into a new one.
The generation itself is pure algorithms, with no AI model inside the mod.

## Download (beta)
1. Download `astray.rhythm-generator.geode` from the
   [latest release](https://github.com/TheAstrayDev/gd-rhythm-generator/releases).
2. Put it into `Geometry Dash/geode/mods` (Geode 5.11.2, GD 2.2091) and restart the game.
3. Open any level in the editor and press **AutoGen** at the top.

It's a beta: the physics constants are taken from GD's own values, but play the result
before you upload it. Bug reports are welcome in [Issues](https://github.com/TheAstrayDev/gd-rhythm-generator/issues).

## How to use
1. In the editor, press **AutoGen** at the top centre.
2. **Choose Song** opens GD's own song menu: song ID field, Newgrounds search, the 2.2 Music
   Library and NCS. The song downloads automatically. With no song chosen, the level's main
   track is used.
3. **Styles** opens the style picker: tick any mix of Classic, Modern, Neon, Tech, Effects,
   Hardcore and Chill (none ticked = all of them). See [Styles](#styles) below.
4. Pick a difficulty (Easy … Demon), a seed (**New**), the sync offset (**−/+**, 10 ms steps,
   plus = later) and the options:
   - **Clear old gen** removes the previous generation: every object of the types the
     generator places;
   - **Effects** turns the triggers on or off.
5. Press **Generate**. The status line at the bottom says when it is done, which style mix the
   level got and whether it was synced on GD's own timeline.

## Styles
A style is a "genome" of 29 knobs: tileset weights, glow, slopes, saws, block design, shader
amount, camera, flashes, screen flips, mirror, entrances, how often the gamemode switches, speed
variety, orbs, pads, platforms, plane flips, robot, wave spam, ship gates, dual portals, UFO /
swing parts, teleports and slams (black and spider orbs).

| Style | Look and feel |
|---|---|
| Classic | RobTop's look: grid and beveled tiles, glow on the edges, pads |
| Modern | outline blocks, sloped corridors, obstacles flying into place, plane flips, teleports |
| Neon | coloured tiles drowned in glow, lots of flashes |
| Tech | mechanical tiles, saws in the walls, ship gates, robot parts |
| Effects | shaders, camera moves, screen flips |
| Hardcore | fast gamemode switches, wave spam, dual portals, teleports, orb slams |
| Chill | calm flow, few switches, soft effects |

The generator never copies a style. Every generation draws random weights over the ticked styles,
blends their knobs, mutates every knob a little (and a few a lot), and then lets each part of the
level drift around that new genome. The status line shows the mix, e.g.
`Tech 39% + Chill 34% + Modern 17%, mutated`.

## How the song is read (`src/Analysis.cpp`)
- **Beat pool:** harmonic / percussive separation (median filtering across time and across
  frequency) splits drums from held notes and voices. Tempo, beats, downbeats and every
  gameplay click come from the percussive part only, so vocals can't pull the beat grid.
- **Vocal pool:** the harmonic part in the voice band (250–3500 Hz) becomes a separate list of
  sung notes (start, strength, how long it is held) and vocal phrases. It drives the robot's
  held jumps and the design: edge glow lights up on every sung note and fades over its length,
  and the camera leans in while a phrase is sung. The gameplay itself stays on the beat.

## How the generation works (`src/gen/`)
Every gamemode uses the same idea: first the player's path is built from the music, then the
obstacles are placed around that path. Each part is chosen by a search: a pool of random
candidates, then annealing cycles that improve the winner. The score is "spectacle": variety of
techniques, hitting the strong beats, no repetition. Options where the physics can't honour a
click are penalised.

**Physics** (`Common.hpp`): GD's own constants, per speed. One unit of GD's velocity is 54 units
per second and gravity is 2916 units/s², at 240 physics steps per second. Cube jump and gravity
are set per speed (0.5x to 4x), the fall speed is capped at 810, and every orb and pad (yellow,
pink, red, blue, black) has its own strength. Ball, robot (hold up to 0.28 s), UFO (click boost,
two gravity stages, slow fall) and swing (gravity flip that keeps 80% of the speed) are simulated
step by step with the same numbers.

- **Cube** (`Cube.cpp`):
  - phrase rhythms follow AAAB / AABB / ABAB / ABAC schemes built from the song's real onsets;
  - 24 candidates and 60 improvement steps for every bar;
  - jumps, yellow / pink / red orbs and pads, dash orbs on long notes, **orb chains** (one orb
    straight into the next), **black orb slams** down onto the beat;
  - plateaus up to 4 blocks, ledge drops, stairs, pillars;
  - switching to a ceiling plane with a blue orb or pad, or snapping there at once with a
    **spider orb / pad** (Harder and up);
  - techniques the level has already used a lot get penalised.
- **Dual cube** (`Dual.cpp`, Hexagon Force style): a ceiling closes over the floor, the dual
  portal splits the cube and the second one falls up onto the ceiling. One button jumps both.
  Spikes stand on the floor and hang from the ceiling in phrases that alternate, mirror or answer
  each other, so the player reads two lanes at once.
- **Ball / Spider** (`Flip.cpp`):
  - flips by click or by a gravity portal;
  - portals only on weaker beats, with calm before and after and no hazards around them;
  - shifting terrain: while the player is on one surface, the other one moves by a block;
  - hazard styles: spike row, spike clusters, block bumps, short row.
- **Wave** (`Wave.cpp`):
  - 18 rhythm templates per bar: beats, eighths, offbeats, gallop, clave, syncopation, bursts,
    "slide and rush", echo, follow the onsets and more;
  - per-bar modifiers: normal, tight, wide or "teeth" corridor, mini size, gravity, and a
    **teleport** that throws the wave three blocks up or down on the downbeat;
  - the path comes from exact physics: the wave never flies flat and never runs into the ceiling.
- **Dual wave** (Blast Processing / Deadlocked style): the wave climbs into a dual portal and a
  mirrored twin appears. Each wave keeps to its own half with a divider between the corridors.
  In bars the search marks "cross", the two fly through each other; they meet again on the exit
  portal.
- **UFO and swing** (`Flyer.cpp`): the clicks sit on the music (eighths, sixteenths on Demon). A
  pilot simulates every click and follows a contour per bar (low, middle, high, rise, fall, swell).
  The corridor pinches on the clicks, saws sit under UFO hops and on the outside of swing turns,
  and **teleport portals** move the player to another lane on a downbeat.
- **Robot** (`Robot.cpp`): the hold length follows the music. Short hits are taps over a spike
  (placed where even the shortest hold clears it). Held notes (drums or voice) are long jumps over
  a spike row or a jump up onto a one- or two-block platform. Big accents are pads over a pit.
- **Ship** (`Ship.cpp`):
  - 18 flight templates: waves, zigzag, terraces, climb, dive, straight fly, M, sawtooth, gates,
    squeeze, open air, offbeat dodge, echo and more;
  - every template has its own corridor shape; size and gravity change per bar.
- **Structure and speed** (`Plan.cpp`):
  - in loud parts the gamemode changes every 2–4 bars (faster in switch-heavy styles); mirror
    and gravity on drops; dual parts in the loud sections;
  - the speed is planned from the song structure: drops and peaks one step faster, the
    breakdown after an intense part one step slower, everything else back at the base speed;
  - two-pass generation for exact sync: the mod first places only the speed portals, asks GD
    itself where the player is at every moment (`LevelEditorLayer::posForTime`), then builds
    all gameplay and effects on that table.

**Corridors** (`Corridor.cpp`): ship, wave, UFO and swing walls are built column by column around
every lane of the path. The walls hug the path with slopes where it climbs or falls, saws are
half-buried in the walls, and lanes that split (teleports, dual waves) get a divider between them.

**Design** (`Design.cpp`), learned from RobTop's official levels:
- every solid block is autotiled from its neighbours: top tiles face the open side, outer
  corners, pillars, caps and inner tiles, each with the rotation / flip RobTop uses;
- six tilesets (grid, beveled, mech, coloured, outline, black), all with full block hitboxes;
  one tileset per song section, so the look changes with the music instead of every few blocks;
- glow only along the faces the player sees (tops of floors, undersides of ceilings, along
  slopes), never over a spike;
- all decoration is non-solid, so the design never changes what is playable.

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
- **Screen flip:** in intense parts the camera turns upside down on a calm downbeat for 2–4
  bars and back before the part ends.
- **Busyness map:** before every screen-wide effect the generator checks how much the player has
  to read right then. Calm: full strength. Busy: a lighter version. Very busy: no effect.

Object IDs and property keys are cross-checked with the
[gmdbuilder](https://github.com/LXtreme/gmdbuilder) tables.

## Building
GitHub Actions (`.github/workflows/build.yml`) builds the mod from the `v6` branch of the Geode
SDK (GD 2.2091) and links it against the Geode 5.11.2 binaries.

Offline test of the analyser and the generator (no GD needed). Every gamemode exports the path it
intends the player to take; the test plays it back against every block, slope, spike and saw on
all six difficulties, and checks the trigger groups and portals:
```bash
python -m ziglang c++ -std=c++20 -O2 -D_USE_MATH_DEFINES test/offline_test.cpp src/Analysis.cpp src/Generator.cpp src/gen/*.cpp -o offline_test.exe
```

## Limitations
- The playthrough test follows the intended path; a player who clicks far off the beat can still
  crash. Play the level before you publish it.
- GD's fonts have no Cyrillic, so the mod's interface is in English.

## Credits
Built with [Claude](https://claude.ai) (Anthropic): the analysis, the generator and the Geode
UI were written with Claude, then tuned through a lot of playtesting.
