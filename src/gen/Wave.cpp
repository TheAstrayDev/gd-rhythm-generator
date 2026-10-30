#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Wave: a library of 18 per-bar rhythm templates (where the wave turns) x corridor styles
// (normal / tight / wide / teeth) x size (normal / mini) x gravity flips x teleports (the wave
// is thrown ~3 blocks up or down on the downbeat and the corridor splits there).
//
// Every bar of a wave part picks one combination. The path is simulated exactly (45 or
// 63 degree slopes, sliding on the floor); if a template would drive the wave into the
// ceiling, an extra turn is inserted ("repair") and the search pays for it. Search: a pool
// of random bar sequences, then annealing. Score: template variety, no immediate repeats,
// turns on real onsets, varied amplitudes, size / gravity changes, few repairs.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {

// toggle positions in sixteenths (bit k = toggle on sixteenth k of the bar)
constexpr int bitsOf(std::initializer_list<int> ks) {
    int m = 0;
    for (int k : ks) m |= 1 << k;
    return m;
}
struct WaveTpl { char const* name; int mask; bool needs16; bool slide; };
// special masks: -1 = follow the onsets of the bar, -2 = repeat the previous bar
constexpr WaveTpl WAVE_TPL[] = {
    {"wave beats", bitsOf({0, 4, 8, 12}), false, false},
    {"wave eighths", bitsOf({0, 2, 4, 6, 8, 10, 12, 14}), false, false},
    {"wave offbeats", bitsOf({2, 6, 10, 14}), false, false},
    {"wave long-short", bitsOf({0, 6, 8, 14}), false, false},
    {"wave gallop", bitsOf({0, 3, 4, 7, 8, 11, 12, 15}), true, false},
    {"wave half glides", bitsOf({0, 8}), false, false},
    {"wave whole glide", bitsOf({0}), false, false},
    {"wave syncopated", bitsOf({0, 3, 6, 10, 12}), true, false},
    {"wave triplet feel", bitsOf({0, 5, 10}), true, false},
    {"wave push", bitsOf({0, 4, 6, 8, 12, 14}), false, false},
    {"wave burst", bitsOf({0, 1, 2, 3, 8}), true, false},
    {"wave late hits", bitsOf({4, 12}), false, false},
    {"wave clave", bitsOf({0, 6, 12}), false, false},
    {"wave stutter", bitsOf({0, 2, 8, 10}), false, false},
    {"wave climb", bitsOf({0, 2, 4, 6}), false, false},
    {"wave slide + rush", bitsOf({8, 10, 12, 14}), false, true},
    {"wave echo", -2, false, false},
    {"wave follow the music", -1, false, false},
};
constexpr int WAVE_TPL_COUNT = int(sizeof(WAVE_TPL) / sizeof(WAVE_TPL[0]));
enum GapStyle { GNormal, GTight, GWide, GTeeth, GAP_COUNT };
constexpr char const* GAP_NAMES[] = {"wave corridor normal", "wave corridor tight", "wave corridor wide", "wave teeth"};

struct BarChoice { int tpl = 0, gap = 0; bool mini = false, grav = false, tp = false; };

// toggle times of a template in one bar
std::vector<double> templateToggles(int tpl, int prevTpl, double bar, double P, bool allow16,
                                    std::function<float(double)> const& onset) {
    std::vector<double> ts;
    int mask = WAVE_TPL[tpl].mask;
    if (mask == -2) mask = prevTpl >= 0 ? WAVE_TPL[prevTpl].mask : WAVE_TPL[0].mask;
    for (int k = 0; k < 16; k++) {
        if (k % 2 && !allow16) continue;
        double t = bar + k * P / 4;
        if (mask == -1 ? (onset(t) > 0.35f || k % 4 == 0) : mask >= 0 && (mask & (1 << k))) ts.push_back(t);
    }
    return ts;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Dual wave (Blast Processing / Deadlocked style): the wave climbs into a dual portal; the
// second wave appears there with reversed gravity, so the two fly mirrored around the
// portal's height - one button, two corridors. They split apart, cross through each other,
// and meet again on the exit portal. The divider between the corridors comes from the
// corridor builder wherever the two separate. If the first wave is a bit off its path when it
// enters, the second one is off by the same amount (mirror axis and path shift together), so
// both keep the full tolerance of their corridor.
// ---------------------------------------------------------------------------------------
void Gen::genDualWave(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    const double yc = 150, umax = 128, ylo = 15;
    double a = s.t0 + 0.45;
    double tUp = -1;
    for (double bt : beats) if (bt >= a) { tUp = bt; break; }
    double td = tUp + (yc - ylo) / v;                  // reaches the dual portal
    double tz = s.t1 - 1.5 * P - (umax + yc) / v;      // the last moment the dual part turns home
    std::vector<double> bars;
    for (size_t k = 0; k < beats.size(); k++) if (down[k] && beats[k] > td && beats[k] < tz) bars.push_back(beats[k]);
    if (tUp < 0 || bars.size() < 2) { s.dual = false; return; }
    int nBars = int(bars.size());
    bool allow16 = diff() >= 4;
    double minGap = std::max(D.minGap, 0.1);
    auto onset = [&](double t) { return A.onsetAt(mt(t), 0.03); };

    // each wave keeps to its own half (|u| between uin and umax), so a divider stands between
    // the corridors; only bars marked "cross" let the waves pass through each other, once
    const double gapBase = D.waveGap * 1.1, uin = gapBase / 2 + 4;
    struct DualPath { std::vector<double> ts, us; double te = 0; int toggles = 0, repairs = 0, crossings = 0, skipped = 0; double score = -1e18; bool feasible = true; };
    auto build = [&](std::vector<int> const& tpl, std::vector<char> const& cross) {
        DualPath w;
        double t = td, u = 0, lastToggle = td;
        int dir = 1, side = 1;
        bool crossed = false;
        w.ts = {td}, w.us = {0};
        double sync = 0, sep = 0;
        auto at = [&](double ct) { return u + dir * v * (ct - t); };
        auto toggleAt = [&](double ct) {
            u = at(ct), t = ct, dir = -dir, lastToggle = ct;
            sep = std::max(sep, std::abs(u));
            w.ts.push_back(t), w.us.push_back(u);
            w.toggles++;
        };
        // a forced turn goes on the last eighth (or sixteenth) before the bound, so it still
        // sits on the music
        auto onGrid = [&](double hit) {
            for (double g : {P / 2, P / 4}) {
                double q = bars[0] + std::floor((hit - bars[0]) / g + 1e-9) * g;
                if (q > lastToggle + 0.06 && q > t) return q;
            }
            return hit;
        };
        // run on to ct: turn before the outer wall and before the divider, or cross over once
        auto repairUntil = [&](double ct, bool crossOk) {
            for (int guard = 0; guard < 400; guard++) {
                double hit;
                if (dir == side) hit = t + (umax - side * u) / v;
                else if (crossOk && !crossed) {
                    double zero = t + side * u / v;
                    if (zero >= ct) break;
                    u = 0, t = zero, side = -side, crossed = true;
                    w.crossings++;
                    continue;
                } else hit = t + (side * u - uin) / v;
                if (hit >= ct || hit <= lastToggle + 0.06) break;
                toggleAt(onGrid(hit));
                w.repairs++;
            }
        };
        std::set<int> usedT;
        int repeats = 0;
        for (int i = 0; i < nBars; i++) {
            usedT.insert(tpl[size_t(i)]);
            if (i && tpl[size_t(i)] == tpl[size_t(i - 1)]) repeats++;
            crossed = false;
            for (double ct : templateToggles(tpl[size_t(i)], i ? tpl[size_t(i - 1)] : -1, bars[size_t(i)], P, allow16, onset)) {
                if (ct >= tz) break;
                repairUntil(ct, cross[size_t(i)]);
                if (ct - lastToggle < minGap) continue;
                // still between the divider's edges (just split or just crossed): no turning back
                if (dir == side && side * at(ct) < uin) { w.skipped++; continue; }
                sync += onset(ct);
                toggleAt(ct);
            }
        }
        repairUntil(tz, false);
        // home: head for the middle and meet on the exit portal
        if (std::abs(at(tz)) > 1 && dir == side) toggleAt(std::max(tz, t));
        double uz = at(std::max(tz, t));
        w.te = std::max(tz, t) + std::abs(uz) / v;
        w.ts.push_back(w.te), w.us.push_back(0);
        for (double uu : w.us) if (std::abs(uu) > umax + 1) w.feasible = false;
        double crossWant = std::max(1.0, nBars / 4.0);
        w.score = 3.0 * usedT.size() - 4.0 * repeats + 1.5 * sync + 5.0 * std::min<double>(w.crossings, crossWant)
                  - 4.0 * std::max(0.0, w.crossings - crossWant) + sep / 20 - 3.0 * w.repairs - 0.5 * w.skipped
                  - (w.feasible ? 0.0 : 1000.0);
        return w;
    };
    auto randomTpl = [&]() {
        int t;
        do t = rndInt(WAVE_TPL_COUNT); while ((WAVE_TPL[t].needs16 && !allow16) || WAVE_TPL[t].slide);
        return t;
    };
    std::vector<int> bestT, cur;
    std::vector<char> bestX, curX;
    DualPath best;
    for (int c = 0; c < 20; c++) {
        std::vector<int> ch;
        std::vector<char> cx;
        for (int i = 0; i < nBars; i++) ch.push_back(randomTpl()), cx.push_back(i > 0 && rnd() < 0.3);
        auto w = build(ch, cx);
        if (w.score > best.score) best = std::move(w), bestT = ch, bestX = cx;
    }
    cur = bestT, curX = bestX;
    double curScore = best.score;
    for (int r = 0; r < 150; r++) {
        double temp = 2.0 * (1.0 - r / 150.0);
        auto ch = cur;
        auto cx = curX;
        size_t k = size_t(rndInt(nBars));
        if (rnd() < 0.3 && k > 0) cx[k] = !cx[k];
        else ch[k] = randomTpl();
        auto w = build(ch, cx);
        if (w.score > curScore || rnd() < std::exp((w.score - curScore) / std::max(0.05, temp))) cur = ch, curX = cx, curScore = w.score;
        if (w.score > best.score) best = std::move(w), bestT = ch, bestX = cx;
    }
    if (!best.feasible) { s.dual = false; return; } // the waves could not be kept in their halves
    for (int t : bestT) used(WAVE_TPL[t].name);
    clicks += best.toggles + 2;

    // ---- the two lanes --------------------------------------------------------------------
    double gap = gapBase;
    auto gapAt = [&](double t) {
        double pinch = 0;
        for (size_t q = 0; q < beats.size(); q++) {
            if (std::abs(beats[q] - t) > 0.15) continue;
            double d = (t - beats[q]) / 0.06;
            pinch = std::max(pinch, (gap - 44) * 0.35 * double(kickAt(beats[q])) * std::exp(-d * d));
        }
        return std::max(44.0, gap - pinch);
    };
    Lane one, two;
    one.add(s.t0, ylo, std::max(gap, 110.0));
    one.add(tUp, ylo, std::max(gap, 110.0));
    one.add(td - 0.02, yc - v * 0.02, gap);
    for (size_t i = 0; i + 1 < best.ts.size(); i++) {
        double t0 = best.ts[i], t1 = best.ts[i + 1];
        int n = std::max(1, int((t1 - t0) / 0.02));
        for (int k = 0; k < n; k++) {
            double t = t0 + (t1 - t0) * k / n, u = best.us[i] + (best.us[i + 1] - best.us[i]) * double(k) / n;
            one.add(t, yc + u, gapAt(t));
            two.add(t, yc - u, gapAt(t));
        }
    }
    double te = best.te;
    one.add(te, yc, gapAt(te));
    two.add(te, yc, gapAt(te));
    // after the exit portal the wave dives back to the floor and slides out
    int lastDir = best.us.size() >= 2 && best.us[best.us.size() - 2] < 0 ? 1 : -1; // heading into the middle
    double tTurn = te, yTurn = yc;
    if (lastDir > 0) { tTurn = te + 0.12; yTurn = yc + v * 0.12; one.add(tTurn, yTurn, gap); }
    double tFloor = tTurn + (yTurn - ylo) / v;
    one.add(tFloor, ylo, std::max(gap, 110.0));
    one.add(s.t1 + 1, ylo, std::max(gap, 110.0));
    if (tFloor > s.t1 - 0.2) { s.dual = false; return; } // no room to come home: plain wave instead
    // saws on the outer walls at the widest splits
    if (s.g.saws > 0.3) {
        auto spec = SAWS[s.g.saws > 0.7 ? 1 : 0];
        for (size_t i = 1; i + 1 < best.ts.size(); i++) {
            if (std::abs(best.us[i]) < umax - 25 || rnd() > s.g.saws) continue;
            bool up = best.us[i] > 0;
            one.bites.push_back({best.ts[i], up, spec.first, spec.second});
            two.bites.push_back({best.ts[i], !up, spec.first, spec.second});
        }
    }
    one.half = two.half = 5;
    corridor(si, {one, two}, tl.x(s.t0) + 75, std::min(tl.x(s.t1) - 30, tl.x(tFloor + 0.2)));
    out.add(obj::PortalDual, tl.x(td), yc);
    out.add(obj::PortalDualOff, tl.x(te), yc);
    used("dual wave");
    if (best.crossings) features["dual wave crossings"] += best.crossings;
    if (best.repairs) features["dual wave auto-turns"] += best.repairs;
    features["dual wave turns"] += best.toggles;
}

void Gen::genWave(size_t si) {
    auto& s = segs[si];
    if (s.dual) {
        genDualWave(si);
        if (s.dual) return;
    }
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    const double ylo = 15, yhi = 210;
    double a = s.t0 + 0.45, b = s.t1 - 0.35;
    if (b <= a) return;
    bool allow16 = diff() >= 4;
    double minGap = std::max(D.minGap, 0.1);
    std::vector<double> bars;
    for (size_t k = 0; k < beats.size(); k++) if (down[k] && beats[k] >= s.t0 - 4 * P && beats[k] < b) bars.push_back(beats[k]);
    if (bars.empty()) return;
    int nBars = int(bars.size());
    bool miniOk = !s.mini && diff() >= 3 && s.level >= 2 && v * 2 * (P / 2) <= 210;
    bool gravOk = diff() >= 2 && s.level >= 2;
    bool tpOk = diff() >= 3 || (diff() == 2 && s.level >= 2);

    // the toggle times a choice asks for in bar i
    auto barToggles = [&](int i, BarChoice const& c, BarChoice const* prev) {
        std::vector<double> ts;
        int mask = WAVE_TPL[c.tpl].mask;
        if (mask == -2) mask = prev ? WAVE_TPL[prev->tpl].mask : WAVE_TPL[0].mask;
        if (mask == -1) {
            for (int k = 0; k < 16; k++) {
                if (k % 2 && !allow16) continue;
                double t = bars[size_t(i)] + k * P / 4;
                if (A.onsetAt(mt(t), 0.03) > 0.35f || k % 4 == 0) ts.push_back(t);
            }
        } else if (mask >= 0) {
            for (int k = 0; k < 16; k++) {
                if (!(mask & (1 << k))) continue;
                if (k % 2 && !allow16) continue;
                ts.push_back(bars[size_t(i)] + k * P / 4);
            }
        }
        return ts;
    };

    struct SizeChange { double t, y; bool toMini; };
    struct GravFlip { double t, y; bool up; };
    struct WavePath {
        std::vector<double> ts, ys;
        std::vector<SizeChange> sizes;
        std::vector<GravFlip> gravs;
        std::vector<size_t> tpIdx;            // first point after each teleport
        std::vector<double> tpT, tpY, tpDy;   // portal time, height, jump
        int toggles = 0, repairs = 0;
        double endFloor = 0; // when the wave touches the floor for the last time
        double score = -1e18;
    };
    auto build = [&](std::vector<BarChoice> const& ch) {
        WavePath w;
        w.ts = {s.t0}, w.ys = {ylo};
        bool curMini = s.mini, gravUp = false;
        double vy = v * (curMini ? 2 : 1);
        double t = s.t0, y = ylo, lastToggle = -1e9, floorIdle = 0, sync = 0;
        int dir = -1;
        std::vector<double> amps;
        auto toggleAt = [&](double ct) {
            double yc = std::clamp(y + dir * vy * (ct - t), ylo, yhi);
            if (dir < 0 && y - vy * (ct - t) < ylo) {
                double tf = t + (y - ylo) / vy;
                floorIdle += ct - tf;
                w.ts.push_back(tf), w.ys.push_back(ylo);
            }
            amps.push_back(std::abs(yc - y));
            w.ts.push_back(ct), w.ys.push_back(yc);
            t = ct, y = yc, dir = -dir, lastToggle = ct;
            w.toggles++;
        };
        std::set<int> used_, gapsUsed;
        int repeats = 0;
        double slideBonus = 0;
        for (int i = 0; i < nBars; i++) {
            auto& c = ch[size_t(i)];
            used_.insert(c.tpl);
            gapsUsed.insert(c.gap);
            if (i > 0 && ch[size_t(i - 1)].tpl == c.tpl && WAVE_TPL[c.tpl].mask != -2) repeats++;
            double barEnd = i + 1 < nBars ? bars[size_t(i + 1)] : b;
            auto want = barToggles(i, c, i > 0 ? &ch[size_t(i - 1)] : nullptr);
            bool sizeDone = false;
            if (c.grav && gravOk && bars[size_t(i)] > a + P && bars[size_t(i)] < b - 4 * P) {
                double gy = std::clamp(y + dir * vy * (bars[size_t(i)] - t), ylo, yhi);
                gravUp = !gravUp;
                w.gravs.push_back({bars[size_t(i)], gy, gravUp});
            }
            // teleport on the downbeat: the wave keeps its heading, ~3 blocks up or down
            double bt = bars[size_t(i)];
            if (c.tp && tpOk && !c.grav && bt > a + 2 * P && bt < b - 3 * P && bt - lastToggle > 0.05 && !nearSpeedNet(bt, P)) {
                double yb = std::clamp(y + dir * vy * (bt - t), ylo, yhi);
                double dy = yb < (ylo + yhi) / 2 ? 95.0 : -95.0;
                if (yb + dy >= ylo + 20 && yb + dy <= yhi - 30) {
                    if (dir < 0 && y - vy * (bt - t) < ylo) {
                        double tf = t + (y - ylo) / vy;
                        floorIdle += bt - tf;
                        w.ts.push_back(tf), w.ys.push_back(ylo);
                    }
                    w.ts.push_back(bt), w.ys.push_back(yb);
                    w.tpIdx.push_back(w.ts.size());
                    w.tpT.push_back(bt), w.tpY.push_back(yb), w.tpDy.push_back(dy);
                    w.ts.push_back(bt), w.ys.push_back(yb + dy);
                    t = bt, y = yb + dy, lastToggle = bt; // no turn before the portal after this
                }
            }
            for (size_t j = 0; j <= want.size(); j++) {
                double ct = j < want.size() ? want[j] : barEnd;
                bool real = j < want.size();
                // repair: never let the wave reach the ceiling before the next turn
                if (dir > 0) {
                    double hit = t + (yhi - 10 - y) / vy;
                    if (hit < ct && hit > lastToggle + 0.06 && hit < b) { toggleAt(hit); w.repairs++; }
                }
                // don't lie on the floor for long: rise again on the next eighth
                if (dir < 0) {
                    double tf = t + (y - ylo) / vy;
                    double limit = (WAVE_TPL[c.tpl].slide ? 2 : 1) * P;
                    if (ct - tf > limit + 0.01) {
                        double g = bars[size_t(i)] + std::ceil((tf + limit - bars[size_t(i)]) / (P / 2)) * (P / 2);
                        if (g < ct - minGap && g > lastToggle + minGap && g < b - 0.3) {
                            toggleAt(g);
                            w.repairs++;
                            sync += A.onsetAt(mt(g), 0.04);
                        }
                    }
                }
                if (!real) break;
                if (ct < a || ct > b - 0.05 || ct - lastToggle < minGap) continue;
                // no turning upwards right under the ceiling (the wave would have no room)
                if (dir < 0 && std::clamp(y + dir * vy * (ct - t), ylo, yhi) > yhi - 10 - vy * 0.1) continue;
                bool wantMini = miniOk ? c.mini : curMini;
                if (wantMini != curMini && !sizeDone) {
                    double yc = std::clamp(y + dir * vy * (ct - t), ylo, yhi);
                    toggleAt(ct);
                    w.sizes.push_back({ct, yc, wantMini});
                    curMini = wantMini, vy = v * (curMini ? 2 : 1), sizeDone = true;
                    sync += A.onsetAt(mt(ct), 0.04);
                    continue;
                }
                sync += A.onsetAt(mt(ct), 0.04);
                toggleAt(ct);
            }
            if (WAVE_TPL[c.tpl].slide) slideBonus += 1.0;
        }
        if (curMini != s.mini) w.sizes.push_back({t, y, s.mini});
        if (gravUp) w.gravs.push_back({b - 2 * P, 45, false});
        vy = v * (s.mini ? 2 : 1);
        // a wave can never fly flat: if it is still climbing, turn it down before the ceiling
        if (dir > 0) { toggleAt(std::max(lastToggle + 0.06, t + (yhi - 10 - y) / vy)); w.repairs++; }
        double tf = t + (y - ylo) / vy;
        if (tf < s.t1) w.ts.push_back(tf), w.ys.push_back(ylo);
        w.ts.push_back(s.t1 + 1), w.ys.push_back(ylo);
        w.endFloor = tf;
        double mean = 0, var = 0;
        for (double v2 : amps) mean += v2;
        mean /= std::max<size_t>(1, amps.size());
        for (double v2 : amps) var += (v2 - mean) * (v2 - mean);
        var = std::sqrt(var / std::max<size_t>(1, amps.size()));
        // style: a "spam" genome likes sixteenth templates and tight / teeth corridors
        double spamBias = 0;
        for (auto& c : ch) {
            if (WAVE_TPL[c.tpl].needs16 || c.gap == GTight || c.gap == GTeeth) spamBias += 1;
            if (c.gap == GWide) spamBias -= 0.5;
        }
        w.score = 3.0 * used_.size() + 1.5 * gapsUsed.size() - 4.0 * repeats + 1.5 * sync + var / 8.0
                  + 3.0 * (s.g.waveSpam - 0.5) * spamBias
                  - 3.0 * w.repairs - 2.5 * std::max(0.0, floorIdle - slideBonus * 2 * P)
                  + 2.0 * std::min<size_t>(w.sizes.size(), 4) + 2.0 * std::min<size_t>(w.gravs.size(), 3)
                  + 3.0 * std::min<size_t>(w.tpDy.size(), 3) * s.g.teleports;
        return w;
    };

    auto randomChoice = [&]() {
        BarChoice c;
        do c.tpl = rndInt(WAVE_TPL_COUNT); while (WAVE_TPL[c.tpl].needs16 && !allow16);
        c.gap = rndInt(GAP_COUNT);
        c.mini = miniOk && rnd() < 0.3;
        c.grav = gravOk && rnd() < 0.12;
        c.tp = tpOk && rnd() < 0.05 + 0.15 * s.g.teleports;
        return c;
    };
    std::vector<BarChoice> bestCh;
    WavePath best;
    for (int cand = 0; cand < 24; cand++) {
        std::vector<BarChoice> ch;
        for (int i = 0; i < nBars; i++) ch.push_back(randomChoice());
        auto w = build(ch);
        if (w.score > best.score) best = std::move(w), bestCh = ch;
    }
    {
        auto cur = bestCh;
        double curScore = best.score;
        int accepted = 0, rounds = 200;
        for (int r = 0; r < rounds; r++) {
            double temp = 2.5 * (1.0 - double(r) / rounds);
            auto ch = cur;
            auto& c = ch[size_t(rndInt(nBars))];
            double m = rnd();
            if (m < 0.55) { auto n = randomChoice(); c.tpl = n.tpl; }
            else if (m < 0.8) c.gap = rndInt(GAP_COUNT);
            else if (m < 0.92) c.mini = miniOk && !c.mini;
            else if (m < 0.96) c.grav = gravOk && !c.grav;
            else c.tp = tpOk && !c.tp;
            auto w = build(ch);
            if (w.score > curScore || rnd() < std::exp((w.score - curScore) / std::max(0.05, temp))) cur = ch, curScore = w.score;
            if (w.score > best.score) best = std::move(w), bestCh = ch, accepted++;
        }
        features["wave improvement steps"] += accepted;
    }
    for (auto& c : bestCh) { used(WAVE_TPL[c.tpl].name); used(GAP_NAMES[c.gap]); }
    if (best.repairs) features["wave auto-turns"] += best.repairs;
    clicks += best.toggles;
    auto& ts = best.ts;
    auto& ys = best.ys;

    auto miniAt = [&](double t) {
        bool m = s.mini;
        for (auto& sc : best.sizes) if (sc.t <= t) m = sc.toMini;
        return m;
    };
    auto barIdx = [&](double t) {
        int i = 0;
        while (i + 1 < nBars && bars[size_t(i + 1)] <= t) i++;
        return i;
    };
    for (auto& sc : best.sizes) {
        out.add(sc.toMini ? obj::PortalSizeMini : obj::PortalSizeNormal, tl.x(sc.t) + 20, std::clamp(sc.y, 45.0, 190.0));
        used(sc.toMini ? "wave size switch (mini)" : "wave size switch (normal)");
    }
    for (auto& g : best.gravs) {
        out.add(g.up ? obj::PortalGravityUp : obj::PortalGravityDown, tl.x(g.t), std::clamp(g.y, 45.0, 190.0));
        used("gravity portal");
    }
    for (size_t k = 0; k < best.tpT.size(); k++) {
        out.add(obj::Teleport, tl.x(best.tpT[k]), std::clamp(best.tpY[k], 45.0, 255.0)).kv(key::TeleportOffset, best.tpDy[k]);
        used("wave teleport");
    }

    // corridor: per-bar style; teeth = sharp narrow bites on every turn
    std::vector<std::pair<double, double>> pinches;
    for (size_t i = 1; i + 1 < ts.size(); i++) {
        int bi = barIdx(ts[i]);
        if (bestCh[size_t(bi)].gap != GTeeth) continue;
        double base = D.waveGap * (miniAt(ts[i]) ? 0.8 : 1.0) * 1.2;
        pinches.push_back({ts[i], (base - (miniAt(ts[i]) ? 34 : 44)) * 0.7});
    }
    for (size_t k = 0; k < beats.size(); k++) {
        if (beats[k] < s.t0 + 0.5 || beats[k] > s.t1 - 0.5) continue;
        float kick = kickAt(beats[k]);
        if (kick > 0.55f && s.level >= 2) {
            double base = D.waveGap * (miniAt(beats[k]) ? 0.8 : 1.0);
            pinches.push_back({beats[k], (base - (miniAt(beats[k]) ? 34 : 44)) * 0.4 * kick});
        }
    }
    auto gapAt = [&](double t) {
        int gi = bestCh[size_t(barIdx(t))].gap;
        double mul = gi == GTight ? 0.8 : gi == GWide ? 1.3 : gi == GTeeth ? 1.2 : 1.0;
        double base = D.waveGap * mul * (miniAt(t) ? 0.8 : 1.0);
        double p = 0;
        for (auto& [pt, amount] : pinches) {
            double d = (t - pt) / 0.06;
            p = std::max(p, amount * std::exp(-d * d));
        }
        return std::max(miniAt(t) ? 34.0 : 44.0, base - p);
    };
    std::vector<double> ts2, ys2, gs2;
    std::vector<size_t> tpIdx2;
    for (size_t i = 0; i + 1 < ts.size(); i++) {
        if (tpIdx2.size() < best.tpIdx.size() && i == best.tpIdx[tpIdx2.size()]) tpIdx2.push_back(ts2.size());
        int n = std::max(1, int((ts[i + 1] - ts[i]) / 0.02));
        for (int k = 0; k < n; k++) {
            double tt = ts[i] + (ts[i + 1] - ts[i]) * k / n;
            ts2.push_back(tt);
            ys2.push_back(ys[i] + (ys[i + 1] - ys[i]) * double(k) / n);
            // sliding on the floor gets head room instead of a low ceiling
            gs2.push_back(ys2.back() <= ylo + 0.5 ? std::max(gapAt(tt), 110.0) : gapAt(tt));
        }
    }
    ts2.push_back(ts.back()), ys2.push_back(ys.back()), gs2.push_back(gapAt(ts.back()));
    // the corridor ends soon after the final landing: the wave slides out on open ground
    double wallEnd = std::min(tl.x(s.t1) - 30, tl.x(best.endFloor + 0.2));
    while (tpIdx2.size() < best.tpIdx.size()) tpIdx2.push_back(ts2.size() - 1);
    corridor(si, splitAtTeleports(ts2, ys2, gs2, tpIdx2, best.tpDy, 5), tl.x(s.t0) + 75, std::max(tl.x(s.t0) + 75, wallEnd));
    used(s.mini ? "mini wave" : "wave");
}

} // namespace rg::gen
