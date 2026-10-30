#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Dual cube (Hexagon Force style). A ceiling closes over the floor and the dual portal splits
// the cube in two: the second cube appears where the first one is with reversed gravity and
// falls up onto the ceiling. One button now jumps both - the floor cube up, the ceiling cube
// down - so every jump is checked against two lanes. Spikes stand on the floor or hang from
// the ceiling under the jump arcs: in phrases that alternate (floor, ceiling, floor, ...),
// mirror (both at once), or answer each other, so the player reads two lanes at once.
//
// The part starts calm (no click until the second cube has landed on the ceiling and the
// player had a moment to see it) and ends with the exit portal on a calm stretch.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

void Gen::genDualCube(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    Ballistic ph{CUBE_PHYS[s.speedRank].g};
    const double JUMP = CUBE_PHYS[s.speedRank].v;
    const double C = diff() >= 4 ? 150 : 180; // ceiling underside: the ceiling cube stands under it
    double a = s.t0 + 0.5, b = s.t1 - 0.9;
    double td = -1;
    for (double bt : beats) if (bt >= a) { td = bt; break; }
    double settle = ph.fall(C - 30, 0, 0) + 0.35;
    double air = ph.fall(0, JUMP, 0);
    if (td < 0 || b - td - settle < 4 * P) { s.dual = false; genCube(si); return; }
    double gapMin = std::max(D.minGap, air + 0.05);
    auto ev = pick(candidates(s, td + settle, b - 0.3, 2), targetCount(s, td + settle, b - 0.3, 0.8), gapMin);
    if (ev.size() < 3) { s.dual = false; genCube(si); return; }
    double te = std::min(b, ev.back().t + air + 0.3);

    // the ceiling: two rows of blocks from a bit before the portal to the exit
    for (double x = tl.x(td) - 60; x <= tl.x(te) + 45; x += 30) {
        int g = chunkGroup(si, s.t0 + (x - tl.x(s.t0)) / v, x, true);
        solidBlock(x, C + 15, g, si);
        solidBlock(x, C + 45, g, si);
    }
    out.add(obj::PortalDual, tl.x(td), 45);
    out.add(obj::PortalDualOff, tl.x(te), 45);

    // spikes under a jump at time t, on the floor (world y 15) or hanging from the ceiling
    double mx = D.margin * 0.5;
    auto safeInterval = [&](double t, double& l, double& r) {
        auto h = [&](double px) {
            double tt = s.t0 + (px - tl.x(s.t0)) / v;
            return tt < t || tt > t + air ? -1.0 : ph.y(0, JUMP, tt - t);
        };
        l = 1e18, r = -1e18;
        for (double sx = tl.x(t); sx <= tl.x(t + air); sx += 2) {
            bool ok = true;
            for (double px = sx - SPIKE_HALF - PLAYER_HALF - mx; px <= sx + SPIKE_HALF + PLAYER_HALF + mx && ok; px += 2)
                if (h(px) < SPIKE_TOP + D.margin) ok = false;
            if (ok) l = std::min(l, sx), r = std::max(r, sx);
        }
        return r >= l;
    };
    // phrase patterns, one letter per jump: F floor, C ceiling, B both, f floor single spike
    static char const* patterns[] = {"FCFC", "FFCC", "FCCF", "BFCB", "CFCF", "FBCB", "FCBF"};
    char const* pat = patterns[rndInt(7)];
    int nFloor = 0, nCeil = 0;
    for (size_t k = 0; k < ev.size(); k++) {
        if (k % 4 == 0 && rnd() < 0.5) pat = patterns[rndInt(7)];
        char p = pat[k % 4];
        if (diff() <= 2 && p == 'B') p = k % 2 ? 'C' : 'F';
        double l, r;
        if (!safeInterval(ev[k].t, l, r)) continue;
        int fit = int(std::floor((r - l) / 30.0)) + 1;
        int n = std::min(fit, 1 + rndInt(D.maxSpikes));
        double c = (l + r) / 2;
        for (int q = 0; q < n; q++) {
            double sx = c + (q - (n - 1) / 2.0) * 30;
            if (p == 'F' || p == 'B') spike(sx, 15, false, chunkGroup(si, ev[k].t, sx, false)), nFloor++;
            if (p == 'C' || p == 'B') spike(sx, C - 15, true, chunkGroup(si, ev[k].t, sx, true)), nCeil++;
        }
        clicks++;
    }
    // the intended paths of both cubes, for the tests
    for (double t = td + settle; t < te; t += 1.0 / 120) {
        double h = 0;
        for (auto& e : ev) if (t >= e.t && t <= e.t + air) h = ph.y(0, JUMP, t - e.t);
        trace.push_back({tl.x(t), 15 + h, PLAYER_HALF});
        trace.push_back({tl.x(t), C - 15 - h, PLAYER_HALF});
    }
    used("dual cube");
    if (nCeil) features["dual cube ceiling spikes"] += nCeil;
    if (nFloor) features["dual cube floor spikes"] += nFloor;
}

} // namespace rg::gen
