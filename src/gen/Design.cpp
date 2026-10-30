#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Design layer, learned from RobTop's official levels (12 levels, ~170k objects analysed):
//
//   * glow lining exposed edges is the single most used object in his levels; it sits in the
//     empty cell next to a face: rot 0 above a surface, flip-Y under a ceiling, rot 90 on the
//     left of a wall, rot 90 + flip-Y on the right
//   * blocks are autotiled from their neighbours: "top" tiles face the open side (rot 0 up,
//     flip-Y down, 270 left, 270 + flip-Y right), outer corners per open corner (0 = up-left,
//     flip-X = up-right, 180 = down-right, 270 = down-left), pillars for one-wide runs
//     (0 vertical, 90 horizontal), caps on pillar ends, a full inner tile inside
//   * chains hang from roomy ceilings, gears turn inside big solid masses
//
// Only tiles with a full 30x30 block hitbox replace gameplay blocks (some "inner" and the
// "outline top" tiles have no or a thin hitbox and would open holes). Everything decorative
// (glow, wall edges, chains, gears) is non-solid, so the design never changes what is playable.
// Each part has two tilesets from its style genome: edges from one, the inside from the other.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
struct TileSet { int top, outer, inner, pillar, cap; };
// all of these have a full 30x30 block hitbox (checked against GD's hitbox table)
constexpr TileSet TILESETS[TILESET_COUNT] = {
    /* grid     */ {2, 3, 83, 7, 6},
    /* beveled  */ {70, 71, 94, 75, 74},
    /* mech     */ {77, 78, 76, 82, 81},
    /* colored  */ {208, 209, 207, 207, 208},
    /* outline  */ {467, 469, 467, 471, 470},
    /* black    */ {91, 1, 94, 1, 91},
};
constexpr int GLOW_LARGE = 1011, GLOW_MEDIUM = 503, CHAIN_SMALL = 110;
constexpr int GEARS[3] = {85, 86, 87};
constexpr int KEY_FLIPX = 4, KEY_FLIPY = 5;
struct Orient { double rot; bool fx, fy; };
} // namespace

void Gen::solidBlock(double x, double y, int group, size_t seg) {
    auto key = std::make_pair(std::lround(x * 2), std::lround(y * 2));
    if (cells.count(key)) return;
    cells[key] = {x, y, group, seg};
}

void Gen::design() {
    auto has = [&](double x, double y) { return cells.count({std::lround(x * 2), std::lround(y * 2)}) > 0; };
    std::set<std::pair<long, long>> decoUsed;
    auto freeCell = [&](double x, double y) {
        auto k = std::make_pair(std::lround(x * 2), std::lround(y * 2));
        if (cells.count(k) || decoUsed.count(k)) return false;
        decoUsed.insert(k);
        return true;
    };
    auto put = [&](int id, double x, double y, Orient o, int group, bool noTouch, int group2 = 0) {
        auto& ob = out.add(id, x, y);
        if (o.rot != 0) ob.kv(key::Rotation, o.rot);
        if (o.fx) ob.kv(KEY_FLIPX, 1);
        if (o.fy) ob.kv(KEY_FLIPY, 1);
        if (noTouch) ob.kv(key::NoTouch, 1);
        ob.groups(group, group2);
    };
    std::map<size_t, int> gearGroups;
    glowGroup = nextGroup++;
    int tiles = 0, glows = 0, chains = 0, gears = 0;
    // design decisions are made once per part (glow along whole edges, never spotty):
    // which sides of the solid structures glow, and how the corridor walls are dressed
    struct PartLook { bool glow[4]; bool wallTiles, wallGlow; };
    std::map<size_t, PartLook> looks;
    auto look = [&](size_t si) -> PartLook const& {
        auto it = looks.find(si);
        if (it != looks.end()) return it->second;
        auto& g = segs[si].g;
        double p = 0.15 + 0.85 * g.glow;
        PartLook L{};
        L.glow[0] = rnd() < p;              // top faces: the most common glow in RobTop's levels
        L.glow[1] = rnd() < p * 0.8;        // under ceilings
        L.glow[2] = L.glow[3] = rnd() < p * 0.7; // the sides
        L.wallTiles = rnd() < 0.3 + 0.7 * g.wallDeco;
        L.wallGlow = rnd() < p;
        return looks[si] = L;
    };

    // ---- solid cells: autotiled blocks + glow + chains ----------------------------------
    for (auto& [k, c] : cells) {
        auto& sg = segs[c.seg];
        auto& g = sg.g;
        bool u = has(c.x, c.y + 30), d = has(c.x, c.y - 30), l = has(c.x - 30, c.y), r = has(c.x + 30, c.y);
        int open = !u + !d + !l + !r;
        TileSet const& ts = TILESETS[sg.tileset];
        TileSet const& inner = TILESETS[sg.tilesetAlt];
        int id = obj::Block;
        Orient o{0, false, false};
        if (g.blockDesign < 0.15) {
            id = obj::Block;
        } else if (open == 0) {
            id = inner.inner;
        } else if (open == 1) {
            id = ts.top;
            if (!u) o = {0, false, false};
            else if (!d) o = {0, false, true};
            else if (!l) o = {270, false, false};
            else o = {270, false, true};
        } else if (open == 2) {
            if (!u && !d) id = ts.pillar, o = {90, false, false};
            else if (!l && !r) id = ts.pillar, o = {0, false, false};
            else {
                id = ts.outer;
                if (!u && !l) o = {0, false, false};
                else if (!u && !r) o = {0, true, false};
                else if (!d && !r) o = {180, false, false};
                else o = {270, false, false};
            }
        } else if (open == 3) {
            id = ts.cap;
            if (d) o = {0, false, false};          // open upwards
            else if (u) o = {0, false, true};      // open downwards
            else if (r) o = {270, false, false};   // open to the left
            else o = {90, false, false};           // open to the right
        } else {
            id = ts.inner;
        }
        put(id, c.x, c.y, o, c.group, false);
        tiles++;

        // glow in the empty cell next to every exposed face (never over a hazard, never
        // under the ground)
        PartLook const& lk = look(c.seg);
        int glowId = g.glow > 0.55 ? GLOW_LARGE : GLOW_MEDIUM;
        auto glowAt = [&](bool openSide, double gx, double gy, Orient go) {
            if (!openSide || gy < 14) return;
            if (nearHazard(gx, gy) || !freeCell(gx, gy)) return;
            put(glowId, gx, gy, go, c.group, true, glowGroup);
            glows++;
        };
        glowAt(!u && lk.glow[0], c.x, c.y + 30, {0, false, false});
        glowAt(!d && lk.glow[1], c.x, c.y - 30, {0, false, true});
        glowAt(!l && lk.glow[2], c.x - 30, c.y, {90, false, false});
        glowAt(!r && lk.glow[3], c.x + 30, c.y, {90, false, true});
    }

    // ---- corridor walls (wave / ship): edge tiles, glow, chains, gears inside -----------
    // Walls are vertically scaled blocks; the design goes on top of them as non-solid deco:
    // a tile row along each face, glow into the corridor, chains from roomy ceilings and
    // turning gears buried in thick walls.
    for (size_t k = 0; k < wallFaces.size(); k++) {
        auto& f = wallFaces[k];
        auto& sg = segs[f.seg];
        auto& g = sg.g;
        TileSet const& ts = TILESETS[sg.tileset];
        bool hasBottom = f.bottomTop >= 30, hasTop = f.topBottom <= 330;
        PartLook const& lk = look(f.seg);
        if (lk.wallTiles) {
            if (hasBottom) put(ts.top, f.x, f.bottomTop - 15, {0, false, false}, f.groupBottom, true);
            if (hasTop) put(ts.top, f.x, f.topBottom + 15, {0, false, true}, f.groupTop, true);
            tiles++;
        }
        if (lk.wallGlow) {
            int glowId = g.glow > 0.55 ? GLOW_LARGE : GLOW_MEDIUM;
            if (hasBottom) put(glowId, f.x, f.bottomTop + 15, {0, false, false}, f.groupBottom, true, glowGroup), glows++;
            if (hasTop) put(glowId, f.x, f.topBottom - 15, {0, false, true}, f.groupTop, true, glowGroup), glows++;
        }
        // a chain hangs only where the corridor is roomy, well clear of the path
        if (hasTop && f.topBottom - f.bottomTop > 150 && k % 3 == 0 && rnd() < 0.35 * g.chains) {
            put(CHAIN_SMALL, f.x, f.topBottom - 15, {0, false, false}, f.groupTop, true);
            chains++;
        }
        // gears turning inside thick walls (fully buried, so they never look like saws)
        double inside = 360 - f.topBottom;
        if (hasTop && inside > 120 && k % 2 == 0 && rnd() < 0.12 * g.gears) {
            if (!gearGroups.count(f.seg)) gearGroups[f.seg] = nextGroup++;
            int gid = inside > 200 ? GEARS[rndInt(2)] : GEARS[2];
            double r = gid == GEARS[2] ? 30 : 50;
            double gy = f.topBottom + r + 15 + rnd() * std::max(0.0, inside - 2 * r - 30);
            out.add(gid, f.x, std::min(gy, 345.0)).kv(key::NoTouch, 1).groups(gearGroups[f.seg], f.groupTop);
            gears++;
        }
    }
    // gears spin through their whole part
    if (O.effects) {
        for (auto& [si, gg] : gearGroups) {
            auto& s = segs[si];
            double dur = s.t1 - s.t0;
            out.add(obj::TrigRotate, tl.x(s.t0), 1290).kv(key::TargetGroup, gg).kv(key::Degrees, 0)
                .kv(key::Times360, std::max(1, int(dur / 1.5))).kv(key::Duration, dur);
        }
    }
    if (tiles) features["designed tiles"] += tiles;
    if (glows) features["edge glow"] += glows;
    if (chains) features["hanging chains"] += chains;
    if (gears) features["wall gears"] += gears;
}

} // namespace rg::gen
