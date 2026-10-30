#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Design layer, learned from RobTop's official levels (12 levels, ~170k objects analysed).
// The rule that matters most is restraint: one block theme per song section, the same glow
// language everywhere, nothing hanging in the play field.
//
//   * blocks are autotiled from their neighbours: "top" tiles face the open side (rot 0 up,
//     flip-Y down, 270 left, 270 + flip-Y right), outer corners per open corner (0 = up-left,
//     flip-X = up-right, 180 = down-right, 270 = down-left), pillars for one-wide runs
//     (0 vertical, 90 horizontal), caps on pillar ends, a full inner tile inside
//   * glow lines the faces the player looks at: above every top face, under every ceiling
//     (never on the sides, never over a hazard) - the single most used object in his levels
//   * the theme comes from the part (one per song section), so a section reads as one place
//
// Only tiles with a full 30x30 block hitbox replace gameplay blocks, and glow is non-solid,
// so the design never changes what is playable. Corridor walls are dressed where they are
// built (Corridor.cpp) with the same theme and glow.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
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
        if (o.fx) ob.kv(key::FlipX, 1);
        if (o.fy) ob.kv(key::FlipY, 1);
        if (noTouch) ob.kv(key::NoTouch, 1);
        ob.groups(group, group2);
    };
    int tiles = 0, glows = 0;
    int glowId = G.glow > 0.55 ? GLOW_LARGE : GLOW_MEDIUM;

    for (auto& [k, c] : cells) {
        auto& sg = segs[c.seg];
        bool u = has(c.x, c.y + 30), d = has(c.x, c.y - 30), l = has(c.x - 30, c.y), r = has(c.x + 30, c.y);
        int open = !u + !d + !l + !r;
        TileSet const& ts = TILESETS[sg.tileset];
        int id = obj::Block;
        Orient o{0, false, false};
        if (sg.g.blockDesign < 0.15) {
            id = obj::Block;
        } else if (open == 0) {
            id = ts.inner;
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

        // glow above top faces and under ceilings (never under the ground, never over a hazard)
        if (!glowOn) continue;
        auto glowAt = [&](double gx, double gy, Orient go) {
            if (gy < 14 || nearHazard(gx, gy) || !freeCell(gx, gy)) return;
            put(glowId, gx, gy, go, c.group, true, glowGroup);
            glows++;
        };
        if (!u) glowAt(c.x, c.y + 30, {0, false, false});
        if (!d) glowAt(c.x, c.y - 30, {0, false, true});
    }
    if (tiles) features["designed tiles"] += tiles;
    if (glows) features["edge glow"] += glows;
}

} // namespace rg::gen
