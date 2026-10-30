#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Robot: the jump height depends on how long the button is held, so the music decides it.
//
// GD's robot (Common.hpp): a click starts a climb at half the cube's jump speed; while the
// button is held (up to 0.28 s) the climb keeps its speed, after the release gravity (0.9 x
// the cube's) takes over. A tap barely hops, a full hold reaches ~3.4 blocks.
//
//   short hit -> tap   : a hop over one spike
//   held note -> hold  : a long, high jump over a spike row; the hold lasts as long as the
//                        note does (sustain from the drum pool, or the vocal pool when a
//                        singer holds a note on the beat)
//   big accent -> pad  : a yellow pad throws the robot over a pit of spikes, no click
//   long note  -> step : the robot jumps up onto a block platform (one or two blocks), rides
//                        it and drops off just before the next hit
//
// Every action has a hold window (a tap is 0.06 - 0.14 s, a hold 0.18 - 0.28 s): spikes go only
// where even the shortest hold of the window clears them, and the next click waits until the
// longest hold of the window has landed - holding a bit too long or too short is still safe as
// long as tap / hold matches the note.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
enum RobotAct { RTap, RHold, RPad, RStep };
struct HoldWindow { double lo, hi; };
constexpr HoldWindow TAP{0.06, 0.14}, HOLD{0.18, ROBOT_HOLD}, STEP{0.12, ROBOT_HOLD}, STEP2{0.24, ROBOT_HOLD};
} // namespace

void Gen::genRobot(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double a = s.t0 + 0.55, b = s.t1 - 0.7;
    if (b <= a) return;
    Ballistic ph{ROBOT_GRAV * CUBE_PHYS[s.speedRank].g};
    const double V = ROBOT_JUMP * CUBE_PHYS[s.speedRank].v;
    double gap = std::max(D.minGap, 0.34);
    auto ev = pick(candidates(s, a, b, 2), targetCount(s, a, b, 0.7 + 0.3 * s.g.orbs), gap);
    double xStart = tl.x(s.t0) + 75;
    double margin = 0.6 * D.margin;

    // height d seconds after the click for a hold of `hold` seconds (local, from the floor)
    auto heldY = [&](double hold, double d) { return d <= hold ? V * d : ph.y(V * hold, V, d - hold); };
    // when a hold of `hold` comes back down to height h
    auto landAt = [&](double hold, double h) { return hold + ph.fall(V * hold, V, h); };
    auto padY = [&](double pv, double d) { return ph.y(0, pv, d); };

    // spikes go only where the lowest trajectory keeps the player clear of them (with margin)
    auto spikesUnder = [&](double t, auto&& lowY, int maxN, double tEnd) {
        double mx = D.margin * 0.5;
        auto safe = [&](double sx) {
            for (double px = sx - SPIKE_HALF - PLAYER_HALF - mx; px <= sx + SPIKE_HALF + PLAYER_HALF + mx; px += 2) {
                double tt = s.t0 + (px - tl.x(s.t0)) / v;
                if (tt < t || tt > tEnd || lowY(tt - t) < SPIKE_TOP + margin) return false;
            }
            return true;
        };
        double l = 1e18, r = -1e18;
        for (double sx = tl.x(t); sx <= tl.x(tEnd); sx += 2) {
            if (sx < xStart || !safe(sx)) continue;
            l = std::min(l, sx), r = std::max(r, sx);
        }
        if (r < l) return 0;
        int fit = std::min(maxN, int(std::floor((r - l) / 30.0)) + 1);
        double c = (l + r) / 2;
        for (int k = 0; k < fit; k++) {
            double sx = c + (k - (fit - 1) / 2.0) * 30;
            spike(sx, 15, false, chunkGroup(si, t, sx, false));
        }
        return fit;
    };

    for (size_t k = 0; k < ev.size(); k++) {
        double t = ev[k].t;
        double tn = k + 1 < ev.size() ? ev[k + 1].t : s.t1 - 0.4;
        float sus = std::max(sustainAt(t), [&] {
            auto vo = A.vocalNear(mt(t), 0.06);
            return vo ? vo->sustain : 0.f;
        }());
        double padHi = PAD_YELLOW * PAD_V, padLo = 0.8 * padHi; // the robot's pad push is uncertain
        double padAir = ph.fall(0, padHi, 0);
        RobotAct act = RTap;
        if (D.pads && ev[k].w > 0.85f && tn - t > padAir + 0.1 && rnd() < 0.35 + 0.4 * s.g.pads) act = RPad;
        else if (sus > 0.22f && tn - t > landAt(HOLD.hi, 0) + 0.08) act = RHold;
        if (act == RHold && tn - t > 0.85 && rnd() < 0.3 + 0.6 * s.g.platforms) act = RStep;
        else if (act == RTap && tn - t < landAt(TAP.hi, 0) + 0.05) continue; // no room to land: leave the hit out
        switch (act) {
            case RPad: {
                double px = tl.x(t);
                if (px < xStart) continue;
                out.add(obj::PadYellow, px, 2).group(chunkGroup(si, t, px, false));
                int n = spikesUnder(t, [&](double d) { return padY(padLo, d); }, 3 + diff() / 2, t + ph.fall(0, padLo, 0));
                if (n) used("robot pad over spikes");
                break;
            }
            case RHold: {
                int n = spikesUnder(t, [&](double d) { return heldY(HOLD.lo, d); }, std::min(3, D.maxSpikes + 1),
                                    t + landAt(HOLD.lo, 0));
                if (n) clicks++, used(n >= 2 ? "robot held jump (row)" : "robot held jump");
                break;
            }
            case RStep: {
                // one or two blocks up, by the length of the note: even the shortest hold of the
                // window is above the front edge (with the margin) when it gets there
                bool two = sus > 0.45f && diff() >= 2;
                HoldWindow w = two ? STEP2 : STEP;
                double top = two ? 60 : 30;
                double need = top + margin;
                double ta = -1;
                for (double d = 0; d < landAt(w.lo, 0); d += 0.005) {
                    if (heldY(w.lo, d) >= need) { ta = d; break; }
                }
                if (ta < 0) continue;
                double xb = tl.x(t + ta) + PLAYER_HALF + D.margin * 0.5;
                double xe = tl.x(tn - 0.04 - ph.fall(top, 0, 0)); // drop off: back on the floor for the next hit
                // the longest hold must have landed on the platform before it ends
                double tl1 = t + landAt(w.hi, top);
                if (xb < xStart || xe - xb < 60 || xe < tl.x(tl1) + PLAYER_HALF) continue;
                for (double bx = xb + 15; bx + 15 <= xe; bx += 30) {
                    int g = chunkGroup(si, t, bx, false);
                    for (double by = 15; by < top; by += 30) solidBlock(bx, by, g, si);
                }
                clicks++;
                used(two ? "robot platform step (2 blocks)" : "robot platform step");
                break;
            }
            case RTap: {
                int n = spikesUnder(t, [&](double d) { return heldY(TAP.lo, d); }, 1, t + landAt(TAP.lo, 0));
                if (n) clicks++, used("robot tap");
                break;
            }
        }
    }
}

} // namespace rg::gen
