#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Robot: the jump height depends on how long the button is held, so the music decides it.
//
//   short hit -> tap   : a short hop over one spike
//   held note -> hold  : a long, high jump over a spike row; the hold lasts as long as the
//                        note does (sustain from the drum pool, or the vocal pool when a
//                        singer holds a note on the beat)
//   big accent -> pad  : a yellow pad throws the robot over a pit of spikes, no click
//   long note  -> step : the robot jumps up onto a block platform, rides it and drops off
//                        just before the next hit (even a light tap clears the front edge)
//
// Every action is checked against a conservative arc (a light tap, a medium hold), and the
// next click waits until that arc has landed, so a player who holds a bit too long or too
// short is still safe as long as tap / hold matches the note.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
constexpr double TAP_V = 0.85;  // light tap, x CUBE_V (apex ~47 units)
constexpr double HOLD_V = 1.15; // medium hold (apex ~86 units)
enum RobotAct { RTap, RHold, RPad, RStep };
} // namespace

void Gen::genRobot(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double a = s.t0 + 0.55, b = s.t1 - 0.7;
    if (b <= a) return;
    double gap = std::max(D.minGap, 0.34);
    auto ev = pick(candidates(s, a, b, 2), targetCount(s, a, b, 0.7 + 0.3 * s.g.orbs), gap);
    double xStart = tl.x(s.t0) + 75;

    // spikes go only where the arc keeps the player clear of them (with the margin)
    auto spikesUnder = [&](double t, double v0, int maxN, double tEnd) {
        auto h = [&](double tt) {
            double d = tt - t;
            return v0 * d - 0.5 * CUBE_G * d * d;
        };
        double mx = D.margin * 0.5;
        auto safe = [&](double sx) {
            for (double px = sx - SPIKE_HALF - PLAYER_HALF - mx; px <= sx + SPIKE_HALF + PLAYER_HALF + mx; px += 2) {
                double tt = s.t0 + (px - tl.x(s.t0)) / v;
                if (tt < t || tt > tEnd || h(tt) < SPIKE_TOP + D.margin) return false;
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
        auto air = [](double v0) { return 2 * v0 * CUBE_V / CUBE_G; };
        RobotAct act = RTap;
        if (D.pads && ev[k].w > 0.85f && tn - t > air(PAD_YELLOW) + 0.1 && rnd() < 0.35 + 0.4 * s.g.pads) act = RPad;
        else if (sus > 0.22f && tn - t > air(HOLD_V) + 0.08) act = RHold;
        if (act == RHold && tn - t > 0.85 && rnd() < 0.3 + 0.6 * s.g.platforms) act = RStep;
        else if (tn - t < air(TAP_V) + 0.05) continue; // no room to land: leave the hit out
        switch (act) {
            case RPad: {
                double px = tl.x(t);
                if (px < xStart) continue;
                out.add(obj::PadYellow, px, 2).group(chunkGroup(si, t, px, false));
                int n = spikesUnder(t, PAD_YELLOW * CUBE_V, 3 + diff() / 2, t + air(PAD_YELLOW));
                if (n) used("robot pad over spikes");
                break;
            }
            case RHold: {
                int n = spikesUnder(t, HOLD_V * CUBE_V, std::min(3, D.maxSpikes + 1), t + air(HOLD_V));
                if (n) clicks++, used(n >= 2 ? "robot held jump (row)" : "robot held jump");
                break;
            }
            case RStep: {
                // front edge: even the light tap is above the block (with the margin) there
                double v0 = TAP_V * CUBE_V, top = 30 + D.margin;
                double disc = v0 * v0 - 2 * CUBE_G * top;
                if (disc <= 0) continue;
                double ta = (v0 - std::sqrt(disc)) / CUBE_G;
                double xb = tl.x(t + ta) + PLAYER_HALF + D.margin * 0.5;
                double xe = tl.x(tn - 0.2);                         // drop off before the next hit
                if (xb < xStart || xe - xb < 60 || xe < tl.x(t + air(HOLD_V) + 0.05) + PLAYER_HALF) continue;
                for (double bx = xb + 15; bx + 15 <= xe; bx += 30) solidBlock(bx, 15, chunkGroup(si, t, bx, false), si);
                clicks++;
                used("robot platform step");
                break;
            }
            case RTap: {
                int n = spikesUnder(t, TAP_V * CUBE_V, 1, t + air(TAP_V));
                if (n) clicks++, used("robot tap");
                break;
            }
        }
    }
}

} // namespace rg::gen
