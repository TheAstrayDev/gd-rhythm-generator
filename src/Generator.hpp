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

enum class Mode { Cube, Ship, Ball, Wave, Spider };
char const* modeName(Mode m);

struct GenOptions {
    Difficulty difficulty = Difficulty::Hard;
    uint32_t seed = 1;
    bool effects = true;       // color / pulse / move / alpha / shake / camera triggers
    double syncOffset = 0;     // seconds, added to every musical time (user calibration)
    double songOffset = 0;     // level's song start offset (seconds)
};

struct GenResult {
    std::string objects;    // "1,8,2,..;1,1,2,..;" (no level header)
    int objectCount = 0;
    int startSpeed = 0;     // GD Speed enum value: 0 = 1x, 1 = 0.5x, 2 = 2x, 3 = 3x, 4 = 4x
    std::string summary;
};

GenResult generate(Analysis const& a, GenOptions const& opt);

// Every object ID the generator can place (used to remove a previous generation).
std::vector<int> generatedObjectIds();

} // namespace rg
