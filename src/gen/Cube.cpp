#include "Common.hpp"

#include <bit>

// ---------------------------------------------------------------------------------------
// Cube gameplay: rhythm grammar + best-of-N search over a physics simulation.
//
//   phrase (4 bars) -> motif scheme AAAB / AABB / ABAB / ABAC / AAAa: the RHYTHM repeats
//                      like the music does
//   bar             -> ~16 candidate realisations of that rhythm are simulated:
//                      every click gets an intent (jump / pad / orb / gravity flip / dash /
//                      drop off a ledge / black-orb slam) and a target height from one of 7
//                      contours (flat, stairs, high plateau, pillars, mountain, walk, cliff)
//   score           -> the most spectacular PLAYABLE candidate wins: variety of actions,
//                      height changes, plane switches, orbs / pads / dashes, orb chains, clicks
//                      on strong onsets, novelty against the previous bars, penalties for
//                      clicks the physics could not honour
//
// Two planes: the floor, and a ceiling plane the cube reaches with a blue pad / blue orb (an arc
// across) or a spider pad / spider orb (an instant snap), runs on upside down (hanging spikes
// and pillars), then flips back. Every flight is simulated in the local frame of its plane with
// GD's own physics for the part's speed (Common.hpp); hazards are placed only where the
// simulated player is clear of them, platforms where it lands.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {

enum Plane { Floor = 0, Ceil = 1 };
enum Intent { IJump, IPad, IOrb, IFlip, IDash, IDrop, IBlack, INTENT_COUNT };
enum Obst { ObSpikes, ObBlocks, ObMixed, ObSpikeBlock };
enum Contour { CFlat, CStairs, CHigh, CPillars, CMountain, CWalk, CCliff, CONTOUR_COUNT };

constexpr double CEIL_Y = 120; // world y of the ceiling plane's surface (4 blocks: stays on screen)

struct Arc {
    double t0, y0, v0, tEnd; // local frame: y = distance from the plane's surface
    bool risky;
    bool dash = false;
    Plane plane = Floor;
};
struct Flight {
    std::vector<Arc> arcs;
    double startH, landH;
    bool special, flip;
    Obst obstacle;
};
struct Pillar { double x0, x1, h, t; Plane plane; bool drop = false; };
struct Placed { int id; double t, x, localY; Plane plane; bool upper; };
struct Span { double x0, x1; };

struct Click { double t; float sustain; float strength; };

// Copyable simulation state: candidates are tried on copies.
struct Sim {
    Plane plane = Floor;
    double surf = 0, lastLand = -1e9, lastFlip = -1e9;
    bool flying = false;
    std::vector<Flight> flights;
    std::vector<Pillar> pillars;
    std::vector<Placed> placed;
    std::vector<Span> ceil;
    std::vector<std::pair<double, bool>> flipsAt; // (time, now upside down) for the camera
    int clicks = 0;
    std::map<std::string, int> used;
    // per-bar score accumulators
    int kinds = 0, heightChanges = 0, flips = 0, specials = 0, skipped = 0, chains = 0;
    double sync = 0;
    std::string sig;
};

} // namespace

void Gen::genCube(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    double a = s.t0 + 0.45, b = s.t1 - 0.6;
    if (b <= a) return;
    int dif = diff();
    int lv = std::clamp(s.level, 0, 3);
    double minGap = std::max(D.minGap, 0.12);
    int maxH = dif == 0 ? 1 : dif == 1 ? 2 : 4;
    bool allowFlip = dif >= 3 && lv >= 2; // plane switches: Harder+, intense parts only
    bool allowDash = D.orbs && dif >= 1;
    bool allowSlam = D.orbs && dif >= 2;   // black orbs
    bool spiderOk = dif >= 3 && rnd() < 0.3 + 0.7 * s.g.slams; // spider pads / orbs for plane switches

    // GD's cube physics at this part's speed
    Ballistic ph{CUBE_PHYS[s.speedRank].g};
    const double JUMP = CUBE_PHYS[s.speedRank].v;
    auto yOf = [&](Arc const& r, double t) { return r.dash ? r.y0 : ph.y(r.y0, r.v0, t - r.t0); };
    auto vOf = [&](Arc const& r, double t) { return r.dash ? 0.0 : ph.v(r.v0, t - r.t0); };
    auto fallTime = [&](double y0, double v0, double h) { return ph.fall(y0, v0, h); };
    auto apexOf = [&](double y0, double v0) { return ph.apex(y0, v0); };

    // ---- rhythm: phrases of 4 bars with repeating motifs ----------------------------
    auto slotAllowed = [&](int k) {
        if (k % 4 == 0) return true;
        if (k % 2 == 0) return dif >= 1;
        return dif >= 4;
    };
    int usableSlots = dif >= 4 ? 16 : dif >= 1 ? 8 : 4;
    auto makeRhythm = [&](double tb, bool fill) {
        std::vector<int> slots;
        double want = 4 * D.density * LEVEL_DENSITY[lv] * (fill ? 1.35 : 1.0);
        int n = std::clamp(int(std::round(want + (rnd() - 0.5))), 1, 6);
        std::vector<std::pair<double, int>> w;
        for (int k = 0; k < 16; k++) {
            if (!slotAllowed(k)) continue;
            double t = tb + k * P / 4;
            double metric = k == 0 ? 1.0 : k % 4 == 0 ? 0.75 : k % 2 == 0 ? 0.45 : 0.3;
            w.push_back({A.onsetAt(mt(t), 0.035) * 1.1 + metric * 0.6 + rnd() * 0.35, k});
        }
        std::sort(w.begin(), w.end(), [](auto& l, auto& r) { return l.first > r.first; });
        for (auto& [score, k] : w) {
            if (int(slots.size()) >= n) break;
            bool ok = true;
            for (int o : slots) if (std::abs(o - k) * P / 4 < minGap) ok = false;
            if (ok) slots.push_back(k);
        }
        std::sort(slots.begin(), slots.end());
        return slots;
    };
    std::vector<double> bars;
    for (size_t k = 0; k < beats.size(); k++) {
        if (down[k] && beats[k] >= a - 4 * P && beats[k] <= b) bars.push_back(beats[k]);
    }
    static char const* schemes[] = {"AAAB", "AABB", "ABAB", "ABAC", "AAAa"};
    std::vector<std::vector<Click>> barClicks;
    std::vector<char> barLetter;
    for (size_t p = 0; p < bars.size(); p += 4) {
        char const* sc = schemes[rndInt(5)];
        std::map<char, std::vector<int>> motifs;
        for (size_t q = 0; q < 4 && p + q < bars.size(); q++) {
            char c = sc[q];
            std::vector<int> slots;
            if (c == 'a') {
                slots = motifs['A'];
                if (!slots.empty()) slots.pop_back(); // variation: drop the last hit
                int extra = rndInt(16);
                bool ok = slotAllowed(extra);
                for (int o : slots) if (std::abs(o - extra) * P / 4 < minGap) ok = false;
                if (ok) slots.push_back(extra), std::sort(slots.begin(), slots.end());
            } else if (motifs.count(c)) slots = motifs[c];
            else motifs[c] = slots = makeRhythm(bars[p + q], q == 3);
            std::vector<Click> cl;
            for (int k : slots) {
                double t = bars[p + q] + k * P / 4;
                if (t < a || t > b) continue;
                cl.push_back({t, sustainAt(t), A.onsetAt(mt(t), 0.04)});
            }
            barClicks.push_back(cl);
            barLetter.push_back(c);
        }
    }

    // ---- simulation primitives ------------------------------------------------------
    auto land = [&](Sim& S) {
        auto& f = S.flights.back();
        S.flying = false;
        S.plane = f.arcs.back().plane;
        S.surf = f.landH;
        S.lastLand = f.arcs.back().tEnd;
        if (S.surf > 0) S.pillars.push_back({tl.x(S.lastLand) + 5, 1e18, S.surf, S.lastLand, S.plane});
        if (S.plane == Ceil && (S.ceil.empty() || S.ceil.back().x1 < 1e17))
            S.ceil.push_back({tl.x(S.lastLand) - 60, 1e18});
    };
    auto closePillar = [&](Sim& S, double x, bool drop = false) {
        if (!S.pillars.empty() && S.pillars.back().x1 > 1e17) S.pillars.back().x1 = x, S.pillars.back().drop = drop;
    };
    // flip gravity at time t from the current state; returns false if not possible. A blue pad /
    // orb sends the cube across in an arc, a spider pad / orb snaps it to the other plane at once.
    auto flipAt = [&](Sim& S, double t, bool grounded, bool spider) {
        if (t - S.lastFlip < 2 * P) return false;
        // only from the ground or a low hop: a high start would clip the ceiling plane
        if (S.plane == Floor && grounded && S.surf > 0) return false;
        if (S.plane == Floor && !grounded && yOf(S.flights.back().arcs.back(), t) > 60) return false;
        Plane np = S.plane == Floor ? Ceil : Floor;
        // the ceiling starts one block before the flip: the arc leading in must pass under it
        if (S.plane == Floor && !grounded) {
            double xf = tl.x(t);
            for (auto& r : S.flights.back().arcs)
                for (double tt = r.t0; tt <= std::min(r.tEnd, t); tt += 0.004)
                    if (tl.x(tt) > xf - 50 && yOf(r, tt) > CEIL_Y - 36) return false;
        }
        double yl = 0, vl = 0;
        if (!grounded) {
            auto& cur = S.flights.back().arcs.back();
            yl = yOf(cur, t), vl = vOf(cur, t);
        } else yl = S.surf;
        // local height above the old surface -> world bottom -> local height in the new frame
        double yb = S.plane == Floor ? yl : CEIL_Y - 30 - yl;
        double ny = np == Floor ? yb : CEIL_Y - 30 - yb;
        // the flip halves the speed: blue orb 0.8 -> 0.4 x jump, blue pad 0.4 x pad push
        double nv = grounded ? -PAD_BLUE * PAD_V : -ORB_BLUE * JUMP;
        (void)vl;
        if (ny < 5) return false;
        double x = tl.x(t);
        Arc arc{t, ny, nv, t + fallTime(ny, nv, 0), true, false, np};
        if (spider) arc = {t, 0, 0, t, true, false, np}; // already on the other plane
        if (grounded) {
            closePillar(S, x + 10);
            S.flights.push_back({{arc}, S.surf, 0, true, true, ObSpikes});
            S.placed.push_back({spider ? obj::PadSpider : obj::PadBlue, t, x + 22, S.surf + 4, S.plane, false});
            S.flying = true;
        } else {
            auto& f = S.flights.back();
            f.arcs.back().tEnd = t;
            f.arcs.push_back(arc);
            f.landH = 0, f.flip = true, f.special = true;
            S.placed.push_back({spider ? obj::OrbSpider : obj::OrbBlue, t, x, yl + PLAYER_HALF, S.plane, true});
            S.clicks++;
        }
        if (np == Ceil) S.ceil.push_back({x - 15, 1e18});
        else if (!S.ceil.empty() && S.ceil.back().x1 > 1e17) S.ceil.back().x1 = x + 30;
        S.lastFlip = t;
        S.flips++;
        S.used[spider ? (grounded ? "spider pad (plane snap)" : "spider orb (plane snap)")
                      : (grounded ? "blue pad (plane switch)" : "blue orb (plane switch)")]++;
        S.flipsAt.push_back({t, np == Ceil});
        return true;
    };

    auto targetOf = [&](Contour c, int i, int n, double surf) {
        int h = 0;
        switch (c) {
            case CFlat: h = 0; break;
            case CStairs: h = std::min(i + 1, maxH); break;
            case CHigh: h = maxH; break;
            case CPillars: h = std::min(1 + (i % 2), maxH); break;
            case CMountain: h = std::min(i < n / 2 ? i + 1 : n - i, maxH); break;
            case CWalk: h = int(surf / 30); break; // the step comes from the plan
            case CCliff: h = i == 0 ? maxH : 0; break;
            default: break;
        }
        return h * 30.0;
    };

    // one realisation of a bar: what every click tries to do
    struct BarPlan {
        std::vector<Intent> in;
        std::vector<int> hv;  // +0/+1 block on top of the contour
        std::vector<int> var; // variant 0..3: obstacle style / orb colour / walk step
        Contour ct = CFlat;
    };

    // apply one bar's clicks following the plan (deterministic: no randomness inside)
    auto runBar = [&](Sim& S, std::vector<Click> const& cl, BarPlan const& pl) {
        S.kinds = S.heightChanges = S.flips = S.specials = S.skipped = S.chains = 0;
        S.sync = 0;
        S.sig.clear();
        int kindMask = 0;
        for (size_t ci = 0; ci < cl.size(); ci++) {
            auto& c = cl[ci];
            if (S.flying && c.t >= S.flights.back().arcs.back().tEnd) land(S);
            double x = tl.x(c.t);
            double nextT = ci + 1 < cl.size() ? cl[ci + 1].t : c.t + P;
            Intent it = pl.in[ci];
            // upside down the player only gets plain jumps (or the way back): readable & fair
            if (S.plane == Ceil && !S.flying && it != IFlip) it = IJump;
            if (S.plane == Ceil && S.flying && it != IFlip) it = IJump;
            int var = pl.var[ci];
            double maxLocal = S.plane == Ceil ? 0 : maxH * 30.0; // flat while upside down
            double base = pl.ct == CWalk ? std::clamp(S.surf + (var % 3 - 1) * 30.0, 0.0, maxH * 30.0)
                                         : targetOf(pl.ct, int(ci), int(cl.size()), S.surf);
            double target = std::min(maxLocal, base + pl.hv[ci] * 30.0);
            if (!S.flying) {
                double rest = S.plane == Ceil ? 0.3 : 0.06; // time to read the flipped view
                if (c.t < S.lastLand + rest) { S.skipped++; continue; }
                if (it == IFlip && allowFlip && flipAt(S, c.t, true, spiderOk && var >= 2)) {
                    kindMask |= 1 << 4;
                    S.sig += 'F';
                    S.sync += 1.5 * c.strength;
                    continue;
                }
                if (it == IDrop && S.surf > 0) {
                    // walk off the ledge on this note (no click), fall down
                    closePillar(S, x - PLAYER_HALF, true);
                    double tgt = std::max(0.0, std::min(target, S.surf - 30));
                    S.flights.push_back({{{c.t, S.surf, 0, c.t + fallTime(S.surf, 0, tgt), false, false, S.plane}},
                                         S.surf, tgt, true, false, ObSpikes});
                    S.flying = true;
                    S.heightChanges++;
                    kindMask |= 1 << 5;
                    S.sig += 'D';
                    S.used["ledge drop"]++;
                    continue;
                }
                bool pad = it == IPad && D.pads;
                int padId = obj::PadYellow;
                double v0 = JUMP;
                if (pad) {
                    if (target > S.surf + 75) padId = obj::PadRed, v0 = PAD_RED * PAD_V;
                    else if (target > S.surf) padId = obj::PadYellow, v0 = PAD_YELLOW * PAD_V;
                    else padId = obj::PadPink, v0 = PAD_PINK * PAD_V;
                }
                if (target > apexOf(S.surf, v0) - 12) target = S.surf;
                if (target > apexOf(S.surf, v0) - 12) target = 0;
                closePillar(S, x + 10);
                if (pad) {
                    S.placed.push_back({padId, c.t, x + 22, S.surf + 4, S.plane, false});
                    S.used[padId == obj::PadRed ? "red pad" : padId == obj::PadPink ? "pink pad" : "yellow pad"]++;
                    S.specials++;
                    kindMask |= 1 << 1;
                    S.sig += 'P';
                    S.sync += 1.5 * c.strength; // a pad on a strong hit: the beat launches you
                } else {
                    S.clicks++;
                    S.used["jump"]++;
                    kindMask |= 1 << 0;
                    S.sig += 'J';
                    S.sync += c.strength;
                }
                if (std::abs(target - S.surf) > 1) S.heightChanges++;
                S.flights.push_back({{{c.t, S.surf, v0, c.t + fallTime(S.surf, v0, target), pad, false, S.plane}},
                                     S.surf, target, pad, false, Obst(var % (dif >= 2 ? 4 : 2))});
                S.flying = true;
                S.sig += char('0' + int(target / 30));
                continue;
            }
            // airborne
            auto& f = S.flights.back();
            auto& cur = f.arcs.back();
            if (cur.dash || c.t - cur.t0 < 0.12 || !D.orbs) { S.skipped++; continue; }
            // in the air towards / under the ceiling plane: nothing but the way back down
            if ((cur.plane == Ceil || f.flip) && it != IFlip) { S.skipped++; continue; }
            double y = yOf(cur, c.t);
            if (y < 25 || y > 175) { S.skipped++; continue; }
            if (it == IFlip && allowFlip && cur.plane == S.plane && flipAt(S, c.t, false, spiderOk && var >= 2)) {
                kindMask |= 1 << 4;
                S.sig += 'f';
                S.sync += 1.5 * c.strength;
                continue;
            }
            if (cur.plane == Ceil || f.flip) { S.skipped++; continue; } // a failed way back: no orb
            if (it == IDash && allowDash && c.sustain > 0.5 * P) {
                double hold = std::min({double(c.sustain), nextT - c.t - 0.12, 3 * P});
                if (hold >= 0.25) {
                    cur.tEnd = c.t;
                    f.arcs.push_back({c.t, y, 0, c.t + hold, true, true, cur.plane});
                    double tgt = target <= y - 10 ? target : 0;
                    f.arcs.push_back({c.t + hold, y, 0, c.t + hold + fallTime(y, 0, tgt), true, false, cur.plane});
                    f.landH = tgt, f.special = true;
                    S.placed.push_back({obj::OrbDashGreen, c.t, x, y + PLAYER_HALF, cur.plane, true});
                    S.clicks++;
                    S.specials += 2;
                    S.used["dash orb"]++;
                    kindMask |= 1 << 3;
                    S.sig += 'S';
                    S.sync += 2.0 * c.strength; // the player holds the held note
                    continue;
                }
            }
            bool chained = f.arcs.size() >= 2 && !cur.dash; // the flight already took an orb
            if (it == IBlack && allowSlam && y >= 45) {
                // black orb: slam straight down onto the floor / a lower platform
                double tgt = target <= y - 30 ? target : 0;
                cur.tEnd = c.t;
                f.arcs.push_back({c.t, y, -ORB_BLACK, c.t + fallTime(y, -ORB_BLACK, tgt), true, false, cur.plane});
                if (std::abs(tgt - f.landH) > 1) S.heightChanges++;
                f.landH = tgt, f.special = true;
                S.placed.push_back({obj::OrbBlack, c.t, x, y + PLAYER_HALF, cur.plane, true});
                S.clicks++;
                S.specials += 2;
                if (chained) S.chains++;
                S.used["black orb slam"]++;
                kindMask |= 1 << 6;
                S.sig += 'K';
                S.sync += 1.8 * c.strength; // the slam lands on the hit
                continue;
            }
            int id = obj::OrbYellow;
            double mult = ORB_YELLOW;
            if (y > 95) id = obj::OrbPink, mult = ORB_PINK;
            else if (target > y + 30 && dif >= 3) id = obj::OrbRed, mult = ORB_RED;
            else if (var == 3) id = obj::OrbPink, mult = ORB_PINK;
            double v0 = JUMP * mult;
            if (target > apexOf(y, v0) - 12) target = 0;
            if (chained) S.chains++, S.used["orb chain"]++;
            cur.tEnd = c.t;
            f.arcs.push_back({c.t, y, v0, c.t + fallTime(y, v0, target), true, false, cur.plane});
            if (std::abs(target - f.landH) > 1) S.heightChanges++;
            f.landH = target, f.special = true;
            S.placed.push_back({id, c.t, x, y + PLAYER_HALF, cur.plane, true});
            S.clicks++;
            S.specials++;
            S.used[id == obj::OrbRed ? "red orb" : id == obj::OrbPink ? "pink orb" : "yellow orb"]++;
            kindMask |= 1 << 2;
            S.sig += 'O';
            S.sync += 1.3 * c.strength;
        }
        S.kinds = std::popcount(unsigned(kindMask));
    };

    // ---- search per bar: a pool of candidates, then hill-climb the winner -------------
    Sim S;
    std::vector<std::string> recent;
    double fancy = 0.45 + 0.25 * lv; // calm parts stay calmer
    if (dif == 0) fancy *= 0.4;
    int poolSize = dif == 0 ? 8 : 24;
    int improveRounds = dif == 0 ? 15 : 60;

    // intent odds follow the part's style genome; at 0.5 every knob gives the base odds
    auto& sg = s.g;
    double wIn[INTENT_COUNT] = {0.34, 0.18 * (0.5 + sg.orbs), 0.14 * (0.5 + sg.pads), 0.10 * (0.5 + sg.orbs),
                                0.12 * (0.5 + sg.platforms), 0.12 * (0.5 + sg.planeFlips),
                                allowSlam ? 0.10 * (0.5 + sg.slams) : 0.0};
    double wSum = 0;
    for (double w : wIn) wSum += w;
    auto randomIntent = [&]() {
        double r = rnd() * wSum;
        static constexpr Intent order[INTENT_COUNT] = {IJump, IOrb, IPad, IDash, IDrop, IFlip, IBlack};
        for (int k = 0; k < INTENT_COUNT; k++) {
            if (r < wIn[k]) return order[k];
            r -= wIn[k];
        }
        return IFlip;
    };
    auto randomPlan = [&](size_t n) {
        BarPlan pl;
        for (size_t i = 0; i < n; i++) {
            pl.in.push_back(randomIntent());
            pl.hv.push_back(rndInt(3) == 0 ? rndInt(2) : 0);
            pl.var.push_back(rndInt(4));
        }
        pl.ct = dif == 0 ? (rnd() < 0.7 ? CFlat : CStairs) : Contour(rndInt(CONTOUR_COUNT));
        return pl;
    };
    auto mutate = [&](BarPlan pl) {
        size_t i = size_t(rndInt(int(pl.in.size())));
        switch (rndInt(5)) {
            case 0: case 1: pl.in[i] = randomIntent(); break;
            case 2: pl.hv[i] = 1 - pl.hv[i]; break;
            case 3: pl.var[i] = rndInt(4); break;
            default: if (dif > 0) pl.ct = Contour(rndInt(CONTOUR_COUNT)); break;
        }
        return pl;
    };

    for (size_t bi = 0; bi < barClicks.size(); bi++) {
        auto& cl = barClicks[bi];
        if (cl.empty()) continue;
        bool lastBars = bi + 2 >= barClicks.size();
        auto evaluate = [&](BarPlan pl, Sim& T) {
            // come back down in time: at the end of the part, or after two bars upside down
            if (S.plane == Ceil && (lastBars || cl.front().t - S.lastFlip > 8 * P)) pl.in[0] = IFlip;
            T = S;
            runBar(T, cl, pl);
            double score = 0;
            score += fancy * (3.0 * T.kinds + 2.0 * T.heightChanges + 5.0 * T.flips + 1.5 * T.specials + 3.0 * T.chains);
            score += 2.0 * T.sync;
            score -= 4.0 * T.skipped;
            for (auto& r : recent) if (r == T.sig) score -= 6; // no copy-paste bars
            if (barLetter[bi] != 'A' && barLetter[bi] != 'a' && !recent.empty() && recent.back() == T.sig) score -= 4;
            if (T.plane == Ceil && lastBars) score -= 50; // must end on the floor
            if (lv == 0) score -= 2.0 * T.flips;
            // level-wide novelty: actions the level already used a lot cost more
            if (styleTotal > 8) {
                for (char ch2 : T.sig) {
                    if (ch2 >= '0' && ch2 <= '9') continue;
                    score -= 6.0 * double(styleUse[ch2]) / styleTotal;
                }
            }
            return score;
        };
        // 1) pool of random candidates, keep the best
        BarPlan bestPlan;
        Sim best;
        double bestScore = -1e18;
        for (int cand = 0; cand < poolSize; cand++) {
            BarPlan pl = randomPlan(cl.size());
            Sim T;
            double sc = evaluate(pl, T);
            if (sc > bestScore) bestScore = sc, bestPlan = pl, best = std::move(T);
        }
        // 2) improvement cycles on the winner (simulated annealing): mutate the current plan,
        //    always take a more spectacular one, sometimes a slightly worse one early on to
        //    escape local optima; the best plan ever seen is what gets built
        int accepted = 0;
        BarPlan curPlan = bestPlan;
        double curScore = bestScore;
        for (int round = 0; round < improveRounds; round++) {
            double temp = 3.0 * (1.0 - double(round) / improveRounds);
            BarPlan pl = mutate(curPlan);
            if (rnd() < 0.3) pl = mutate(pl); // sometimes a bigger step
            Sim T;
            double sc = evaluate(pl, T);
            if (sc > curScore || rnd() < std::exp((sc - curScore) / std::max(0.05, temp))) {
                curPlan = pl, curScore = sc;
            }
            if (sc > bestScore) bestScore = sc, bestPlan = pl, best = std::move(T), accepted++;
        }
        if (accepted > 0) features["improved bars"]++;
        features["improvement steps"] += accepted;
        S = std::move(best);
        for (char ch2 : S.sig) if (ch2 < '0' || ch2 > '9') styleUse[ch2]++, styleTotal++;
        recent.push_back(S.sig);
        if (recent.size() > 3) recent.erase(recent.begin());
    }
    if (S.flying) land(S);
    if (S.plane == Ceil) {
        // safety net: a blue pad right after landing brings the cube back to the floor
        double t = std::min(b, S.lastLand + 0.15);
        flipAt(S, t, true, false);
        if (S.flying) land(S);
    }
    closePillar(S, tl.x(s.t1) - 45);
    if (!S.ceil.empty() && S.ceil.back().x1 > 1e17) S.ceil.back().x1 = tl.x(s.t1) - 30;
    clicks += S.clicks;
    for (auto& [k, n] : S.used) features[k] += n;

    // pattern space (log10): rhythm subsets x intents x heights x contours x schemes
    {
        int n = std::clamp(int(std::round(4 * D.density * LEVEL_DENSITY[3])), 1, 6);
        double combos = 1;
        for (int i = 0; i < n; i++) combos = combos * (usableSlots - i) / (i + 1);
        double perClick = double(INTENT_COUNT) /*intents*/ * (maxH + 1) /*heights*/ * 3 /*orb/pad variants*/ * (dif >= 2 ? 4 : 2);
        double space = std::log10(std::max(1.0, combos)) + n * std::log10(perClick) + std::log10(5.0 * int(CONTOUR_COUNT));
        patternSpace = std::max(patternSpace, space);
    }

    // ---- the intended path, for the tests ------------------------------------------------
    {
        size_t fi = 0;
        double surf = 0;
        Plane pl = Floor;
        for (double t = s.t0 + 0.5; t < s.t1 - 0.3; t += 1.0 / 120) {
            while (fi < S.flights.size() && S.flights[fi].arcs.back().tEnd < t) {
                surf = S.flights[fi].landH, pl = S.flights[fi].arcs.back().plane;
                fi++;
            }
            double yl = surf;
            Plane p = pl;
            if (fi < S.flights.size() && t >= S.flights[fi].arcs.front().t0) {
                for (auto& r : S.flights[fi].arcs) if (t >= r.t0 && t <= r.tEnd) yl = std::max(0.0, yOf(r, t)), p = r.plane;
            }
            trace.push_back({tl.x(t), p == Floor ? yl + 15 : CEIL_Y - yl - 15, PLAYER_HALF});
        }
    }

    // ---- emit objects -----------------------------------------------------------------
    // local -> world for an object centred at local height lc on plane p
    auto worldY = [&](double lc, Plane p) { return p == Floor ? lc : CEIL_Y - lc; };
    // camera pulls back while the cube is upside down so both planes stay in view
    if (O.effects) {
        for (auto& [ft, up] : S.flipsAt) {
            out.add(obj::TrigZoom, tl.x(ft), 930).kv(key::Zoom, up ? 0.85 : 1.0).kv(key::Duration, 0.35)
                .kv(key::Easing, ease::InOut);
        }
    }
    for (auto& c : S.ceil) {
        if (c.x1 <= c.x0) continue;
        for (double x = c.x0; x <= c.x1; x += 30) {
            int g = chunkGroup(si, s.t0 + (x - tl.x(s.t0)) / v, x, true);
            solidBlock(x, CEIL_Y + 15, g, si);
            solidBlock(x, CEIL_Y + 45, g, si);
        }
        used("ceiling plane");
    }
    for (auto& p : S.placed) {
        bool isPad = p.id == obj::PadYellow || p.id == obj::PadPink || p.id == obj::PadRed || p.id == obj::PadBlue ||
                     p.id == obj::PadSpider;
        double wy = p.plane == Floor ? p.localY : (isPad ? CEIL_Y - p.localY : CEIL_Y - 30 - (p.localY - PLAYER_HALF) + PLAYER_HALF);
        if (p.plane == Ceil && !isPad) wy = CEIL_Y - p.localY; // orb centre = player centre
        auto& o = out.add(p.id, p.x, wy);
        if (p.plane == Ceil && isPad) o.kv(key::Rotation, 180);
        o.group(chunkGroup(si, p.t, p.x, p.upper));
    }
    // a pillar the cube walks off must end exactly at the drop edge (GD keeps the cube up while
    // any part of its box is over a block): its block grid is shifted to end there
    auto pillarSpan = [&](Pillar const& p) {
        double n = std::ceil((p.x1 - p.x0) / 30.0), x0 = p.x0;
        if (p.drop) {
            double d = std::fmod(p.x1 - p.x0, 30.0);
            if (d <= 8 && p.x1 - p.x0 >= 30) n = std::floor((p.x1 - p.x0) / 30.0);
            x0 = p.x1 - 30 * n;
        }
        return std::pair<double, double>{x0, x0 + 30 * n};
    };
    for (auto& p : S.pillars) {
        auto [px0, px1] = pillarSpan(p);
        for (double c = px0 + 15; c < px1; c += 30) {
            for (double y = 15; y < p.h; y += 30) {
                solidBlock(c, worldY(y, p.plane), chunkGroup(si, p.t, c, p.plane == Ceil), si);
            }
        }
        used(p.plane == Ceil ? "hanging platform" : p.h > 60 ? "high plateau" : p.h > 30 ? "tower" : "platform");
    }
    auto inPillar = [&](double sx, Plane pl) {
        for (auto& p : S.pillars) {
            if (p.plane != pl) continue;
            auto [left, right] = pillarSpan(p);
            if (sx + 15 > left - 2 && sx - 15 < right + 2) return true;
        }
        return false;
    };
    auto onCeil = [&](double sx) {
        for (auto& c : S.ceil) if (sx >= c.x0 - 15 && sx <= c.x1 + 15) return true;
        return false;
    };

    // ---- hazards under every flight (in the flight's plane) --------------------------
    for (auto& fl : S.flights) {
        if (fl.flip) continue; // plane switches stay clean
        Plane pl = fl.arcs.front().plane;
        if (pl == Ceil) {
            bool all = true;
            for (auto& r : fl.arcs) if (!onCeil(tl.x(r.t0))) all = false;
            if (!all) continue;
        }
        double ft0 = fl.arcs.front().t0, ft1 = fl.arcs.back().tEnd;
        double fx0 = tl.x(ft0);
        auto yAtX = [&](double px, double& extra) {
            double t = ft0 + (px - fx0) / v;
            extra = 0;
            if (t <= ft0 || t >= ft1) return 0.0;
            for (auto& r : fl.arcs) {
                if (t >= r.t0 && t <= r.tEnd) {
                    extra = r.risky ? 18 : 0;
                    return std::max(0.0, yOf(r, t));
                }
            }
            return 0.0;
        };
        auto safe = [&](double sx, double top, double half) {
            double mx = D.margin * 0.5;
            for (double px = sx - half - PLAYER_HALF - mx; px <= sx + half + PLAYER_HALF + mx; px += 2) {
                double extra;
                double y = yAtX(px, extra);
                if (y < top + D.margin + extra) return false;
            }
            return true;
        };
        std::vector<std::pair<double, double>> iv;
        double xs = fx0, xe = tl.x(ft1);
        bool in = false;
        double st = 0;
        for (double sx = xs; sx <= xe; sx += 2) {
            bool ok = safe(sx, SPIKE_TOP, SPIKE_HALF) && !inPillar(sx, pl) && (pl == Floor || onCeil(sx));
            if (ok && !in) in = true, st = sx;
            if (!ok && in) in = false, iv.push_back({st, sx - 2});
        }
        if (in) iv.push_back({st, xe});

        bool pit = fl.special || fl.startH > 0 || fl.landH > 0;
        auto spikeAt = [&](double sx, double lc, double t) {
            spike(sx, worldY(lc, pl), pl == Ceil, chunkGroup(si, t, sx, pl == Ceil));
        };
        for (auto& [l, r] : iv) {
            int fit = int(std::floor((r - l) / 30.0)) + 1;
            double c = (l + r) / 2;
            double tMid = ft0 + (c - fx0) / v;
            if (pit) {
                int n = std::min(fit, 16);
                for (int k = 0; k < n; k++) spikeAt(c + (k - (n - 1) / 2.0) * 30, 15, tMid);
                used(pl == Ceil ? "hanging spike pit" : "spike pit");
                continue;
            }
            int n = std::min(fit, 1 + rndInt(pl == Ceil ? std::min(2, D.maxSpikes) : D.maxSpikes));
            Obst ob = fl.obstacle;
            if (ob == ObMixed) ob = rnd() < 0.5 ? ObSpikes : ObBlocks;
            if (ob == ObSpikeBlock && safe(c, 30 + SPIKE_TOP, 15)) {
                int g = chunkGroup(si, tMid, c, pl == Ceil);
                solidBlock(c, worldY(15, pl), g, si);
                spike(c, worldY(45, pl), pl == Ceil, g);
                used("spike on block");
                continue;
            }
            if (ob == ObBlocks && safe(c, 30, 15)) {
                int nb = std::max(1, std::min(n, fit));
                for (int k = 0; k < nb; k++) {
                    double bx = c + (k - (nb - 1) / 2.0) * 30;
                    if (!safe(bx, 30, 15)) continue;
                    solidBlock(bx, worldY(15, pl), chunkGroup(si, tMid, bx, pl == Ceil), si);
                }
                used("block obstacle");
                continue;
            }
            for (int k = 0; k < n; k++) spikeAt(c + (k - (n - 1) / 2.0) * 30, 15, tMid);
            used(n == 1 ? "single spike" : n == 2 ? "double spike" : "triple spike");
        }
    }
}

} // namespace rg::gen
