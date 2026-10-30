#pragma once

// Style genome: ~25 knobs (0..1, 0.5 = neutral) that steer design, effects and gameplay.
// The user ticks styles; the generator never copies one of them. It draws random mix weights
// over the ticked styles (Dirichlet), averages their genomes, mutates every knob, and then
// lets every part of the level drift around that new genome - a new style per level, a
// variation of it per part.

#include "../Generator.hpp"

#include <random>
#include <string>

namespace rg::gen {

enum StyleId { StClassic, StModern, StNeon, StTech, StEffects, StHardcore, StChill, STYLE_COUNT };
char const* styleName(int id);

enum Tileset { TsGrid, TsBeveled, TsMech, TsColored, TsOutline, TsBlack, TILESET_COUNT };

struct Genome {
    // design
    double tiles[TILESET_COUNT] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
    double glow = 0.5, slopes = 0.6, saws = 0.4, blockDesign = 0.8;
    // effects
    double fx = 0.5, camera = 0.5, flashes = 0.5, screenFlip = 0.2, mirror = 0.3, entrances = 0.6;
    // gameplay
    double switchRate = 0.5, speedVariety = 0.5, orbs = 0.5, pads = 0.5, platforms = 0.5;
    double planeFlips = 0.4, robot = 0.4, waveSpam = 0.4, shipGates = 0.4;
    double dual = 0.4, flyers = 0.5, teleports = 0.4, slams = 0.4; // dual portals, ufo / swing, teleports, black / spider orbs

    template <class F> void each(F&& f) {
        for (double& t : tiles) f(t);
        for (double* v : {&glow, &slopes, &saws, &blockDesign, &fx, &camera, &flashes, &screenFlip, &mirror,
                          &entrances, &switchRate, &speedVariety, &orbs, &pads, &platforms, &planeFlips, &robot,
                          &waveSpam, &shipGates, &dual, &flyers, &teleports, &slams})
            f(*v);
    }
};

Genome stylePreset(int id);

struct StyleMix {
    Genome genome;
    std::string description; // e.g. "Neon 46% + Tech 31% + Classic 23%, mutated"
};

// mask: bit per StyleId (0 = all styles)
StyleMix blendStyles(uint32_t mask, std::mt19937& rng);
// a part-level variation around the level genome
Genome varyGenome(Genome const& g, std::mt19937& rng, double amount);
// pick a tileset by the genome's weights
int pickTileset(Genome const& g, std::mt19937& rng);

} // namespace rg::gen
