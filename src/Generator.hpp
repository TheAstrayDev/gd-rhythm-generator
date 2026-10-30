#pragma once

// Pure C++ level generator (no Geode / GD dependencies). Produces a GD object string
// that can be fed to LevelEditorLayer::createObjectsFromString.

#include "Analysis.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rg {

enum class Difficulty { Easy = 0, Normal, Hard, Harder, Insane, Demon };
inline constexpr int DIFFICULTY_COUNT = 6;
char const* difficultyName(Difficulty d);

enum class Mode { Cube, Ship, Ball, Wave, Spider, Robot };
char const* modeName(Mode m);

struct GenOptions {
    Difficulty difficulty = Difficulty::Hard;
    uint32_t seed = 1;
    uint32_t styleMask = 0;    // bit per style (Classic, Modern, Neon, Tech, Effects, Hardcore, Chill); 0 = all
    bool effects = true;       // color / pulse / move / alpha / shake / camera triggers
    double syncOffset = 0;     // seconds, added to every musical time (user calibration)
    double songOffset = 0;     // level's song start offset (seconds)

    // Two-pass generation for exact sync with speed changes:
    //  pass 1 (speedPortalsOnly): only the planned speed portals are produced; the mod places
    //          them and asks GD itself where the player is at every moment (posForTime)
    //  pass 2 (timeline set): everything else is built on GD's own time -> x table
    bool speedPortalsOnly = false;
    std::vector<double> timelineX; // x at t = i * timelineStep (from GD), empty = use the model
    double timelineStep = 0.01;
};

struct GenResult {
    std::string objects;    // "1,8,2,..;1,1,2,..;" (no level header)
    int objectCount = 0;
    int startSpeed = 0;     // GD Speed enum value: 0 = 1x, 1 = 0.5x, 2 = 2x, 3 = 3x, 4 = 4x
    std::string summary;
    std::string features;   // which gameplay / effect techniques were used, with counts
    std::vector<double> modelX; // pass 1: the generator's own x at t = i * timelineStep
    double duration = 0;        // level time covered (seconds)
    std::string style;          // the style mix this level was generated with
};

GenResult generate(Analysis const& a, GenOptions const& opt);

// Every object ID the generator can place (used to remove a previous generation).
std::vector<int> generatedObjectIds();

} // namespace rg
