#include "Style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rg::gen {

char const* styleName(int id) {
    static char const* names[] = {"Classic", "Modern", "Neon", "Tech", "Effects", "Hardcore", "Chill"};
    return names[std::clamp(id, 0, STYLE_COUNT - 1)];
}

// Presets distilled from how GD levels look and play: RobTop's official levels (grid / beveled
// tiles, glow lining the edges, ufo parts), modern layouts (outline blocks and slopes, little
// deco, swing, teleports, black-orb slams), neon (coloured blocks drowned in glow and flashes),
// tech (mechanical blocks, saws, ship gates, robot), effect showcases, hardcore switch-ups
// (dual portals, wave spam, slams) and chill flow levels.
Genome stylePreset(int id) {
    Genome g;
    auto tiles = [&](double grid, double bev, double mech, double col, double outl, double black) {
        g.tiles[TsGrid] = grid, g.tiles[TsBeveled] = bev, g.tiles[TsMech] = mech;
        g.tiles[TsColored] = col, g.tiles[TsOutline] = outl, g.tiles[TsBlack] = black;
    };
    switch (id) {
        case StClassic:
            tiles(0.9, 0.8, 0.2, 0.1, 0.1, 0.4);
            g.glow = 0.7, g.slopes = 0.5, g.saws = 0.3, g.blockDesign = 0.9;
            g.fx = 0.3, g.camera = 0.3, g.flashes = 0.5, g.screenFlip = 0.05, g.mirror = 0.2, g.entrances = 0.3;
            g.switchRate = 0.4, g.speedVariety = 0.5, g.orbs = 0.5, g.pads = 0.7, g.platforms = 0.6;
            g.planeFlips = 0.3, g.robot = 0.5, g.waveSpam = 0.3, g.shipGates = 0.3;
            g.dual = 0.4, g.flyers = 0.5, g.teleports = 0.2, g.slams = 0.3;
            break;
        case StModern:
            tiles(0.1, 0.2, 0.2, 0.2, 0.9, 0.5);
            g.glow = 0.3, g.slopes = 0.8, g.saws = 0.5, g.blockDesign = 0.7;
            g.fx = 0.5, g.camera = 0.6, g.flashes = 0.4, g.screenFlip = 0.2, g.mirror = 0.3, g.entrances = 0.9;
            g.switchRate = 0.6, g.speedVariety = 0.6, g.orbs = 0.6, g.pads = 0.4, g.platforms = 0.8;
            g.planeFlips = 0.6, g.robot = 0.3, g.waveSpam = 0.4, g.shipGates = 0.5;
            g.dual = 0.5, g.flyers = 0.7, g.teleports = 0.6, g.slams = 0.6;
            break;
        case StNeon:
            tiles(0.1, 0.1, 0.1, 0.9, 0.4, 0.2);
            g.glow = 1.0, g.slopes = 0.6, g.saws = 0.4, g.blockDesign = 0.8;
            g.fx = 0.7, g.camera = 0.5, g.flashes = 0.9, g.screenFlip = 0.2, g.mirror = 0.3, g.entrances = 0.7;
            g.switchRate = 0.6, g.speedVariety = 0.5, g.orbs = 0.6, g.pads = 0.5, g.platforms = 0.5;
            g.planeFlips = 0.4, g.robot = 0.3, g.waveSpam = 0.6, g.shipGates = 0.4;
            g.dual = 0.5, g.flyers = 0.5, g.teleports = 0.5, g.slams = 0.4;
            break;
        case StTech:
            tiles(0.2, 0.2, 1.0, 0.2, 0.3, 0.4);
            g.glow = 0.4, g.slopes = 0.5, g.saws = 0.9, g.blockDesign = 0.9;
            g.fx = 0.4, g.camera = 0.4, g.flashes = 0.4, g.screenFlip = 0.15, g.mirror = 0.2, g.entrances = 0.6;
            g.switchRate = 0.5, g.speedVariety = 0.5, g.orbs = 0.4, g.pads = 0.5, g.platforms = 0.7;
            g.planeFlips = 0.4, g.robot = 0.8, g.waveSpam = 0.4, g.shipGates = 0.9;
            g.dual = 0.4, g.flyers = 0.4, g.teleports = 0.6, g.slams = 0.5;
            break;
        case StEffects:
            tiles(0.3, 0.3, 0.3, 0.5, 0.5, 0.4);
            g.glow = 0.7, g.slopes = 0.6, g.saws = 0.4, g.blockDesign = 0.6;
            g.fx = 1.0, g.camera = 1.0, g.flashes = 0.9, g.screenFlip = 0.7, g.mirror = 0.6, g.entrances = 1.0;
            g.switchRate = 0.5, g.speedVariety = 0.7, g.orbs = 0.5, g.pads = 0.5, g.platforms = 0.5;
            g.planeFlips = 0.5, g.robot = 0.3, g.waveSpam = 0.4, g.shipGates = 0.4;
            g.dual = 0.6, g.flyers = 0.5, g.teleports = 0.7, g.slams = 0.5;
            break;
        case StHardcore:
            tiles(0.2, 0.3, 0.4, 0.3, 0.4, 0.8);
            g.glow = 0.3, g.slopes = 0.7, g.saws = 0.7, g.blockDesign = 0.6;
            g.fx = 0.4, g.camera = 0.5, g.flashes = 0.5, g.screenFlip = 0.4, g.mirror = 0.6, g.entrances = 0.6;
            g.switchRate = 1.0, g.speedVariety = 0.9, g.orbs = 0.9, g.pads = 0.5, g.platforms = 0.6;
            g.planeFlips = 0.8, g.robot = 0.5, g.waveSpam = 1.0, g.shipGates = 0.7;
            g.dual = 0.9, g.flyers = 0.7, g.teleports = 0.8, g.slams = 0.9;
            break;
        case StChill:
        default:
            tiles(0.5, 0.6, 0.2, 0.4, 0.3, 0.3);
            g.glow = 0.6, g.slopes = 0.6, g.saws = 0.1, g.blockDesign = 0.8;
            g.fx = 0.2, g.camera = 0.2, g.flashes = 0.3, g.screenFlip = 0.0, g.mirror = 0.05, g.entrances = 0.4;
            g.switchRate = 0.15, g.speedVariety = 0.2, g.orbs = 0.4, g.pads = 0.5, g.platforms = 0.5;
            g.planeFlips = 0.1, g.robot = 0.3, g.waveSpam = 0.1, g.shipGates = 0.2;
            g.dual = 0.2, g.flyers = 0.5, g.teleports = 0.1, g.slams = 0.1;
            break;
    }
    return g;
}

StyleMix blendStyles(uint32_t mask, std::mt19937& rng) {
    std::vector<int> ids;
    for (int i = 0; i < STYLE_COUNT; i++) if (!mask || (mask & (1u << i))) ids.push_back(i);
    // Dirichlet(1) weights: a different mix every time
    std::gamma_distribution<double> gamma(1.0, 1.0);
    std::vector<double> w;
    double total = 0;
    for (size_t i = 0; i < ids.size(); i++) w.push_back(gamma(rng)), total += w.back();
    std::vector<Genome> gs;
    for (int id : ids) gs.push_back(stylePreset(id));
    StyleMix mix;
    Genome out;
    {
        // average knob by knob
        std::vector<std::vector<double*>> knobs(gs.size());
        for (size_t i = 0; i < gs.size(); i++) gs[i].each([&](double& v) { knobs[i].push_back(&v); });
        std::vector<double*> dst;
        out.each([&](double& v) { dst.push_back(&v); });
        for (size_t k = 0; k < dst.size(); k++) {
            double v = 0;
            for (size_t i = 0; i < gs.size(); i++) v += *knobs[i][k] * w[i] / total;
            *dst[k] = v;
        }
    }
    // mutation: every knob moves a little, a few move a lot
    std::normal_distribution<double> small(0, 0.08);
    std::uniform_real_distribution<double> u(0, 1);
    out.each([&](double& v) {
        v += small(rng);
        if (u(rng) < 0.12) v += (u(rng) - 0.5) * 0.6;
        v = std::clamp(v, 0.0, 1.0);
    });
    mix.genome = out;
    std::vector<std::pair<double, int>> order;
    for (size_t i = 0; i < ids.size(); i++) order.push_back({w[i] / total, ids[i]});
    std::sort(order.rbegin(), order.rend());
    char buf[64];
    for (auto& [share, id] : order) {
        if (share < 0.05) continue;
        if (!mix.description.empty()) mix.description += " + ";
        std::snprintf(buf, sizeof(buf), "%s %d%%", styleName(id), int(std::lround(share * 100)));
        mix.description += buf;
    }
    mix.description += ", mutated";
    return mix;
}

Genome varyGenome(Genome const& g, std::mt19937& rng, double amount) {
    Genome out = g;
    std::normal_distribution<double> n(0, amount);
    out.each([&](double& v) { v = std::clamp(v + n(rng), 0.0, 1.0); });
    return out;
}

int pickTileset(Genome const& g, std::mt19937& rng) {
    double total = 0;
    for (double t : g.tiles) total += t * t + 1e-3; // squared: the style's favourites dominate
    double r = std::uniform_real_distribution<double>(0, total)(rng);
    for (int i = 0; i < TILESET_COUNT; i++) {
        r -= g.tiles[i] * g.tiles[i] + 1e-3;
        if (r <= 0) return i;
    }
    return TsBlack;
}

} // namespace rg::gen
