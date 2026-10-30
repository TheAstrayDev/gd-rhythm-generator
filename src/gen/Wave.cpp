#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Wave: a library of 18 per-bar rhythm templates (where the wave turns) x corridor styles
// (normal / tight / wide / teeth) x size (normal / mini) x gravity flips.
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

struct BarChoice { int tpl = 0, gap = 0; bool mini = false, grav = false; };

} // namespace

void Gen::genWave(size_t si) {
    auto& s = segs[si];
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
                  + 2.0 * std::min<size_t>(w.sizes.size(), 4) + 2.0 * std::min<size_t>(w.gravs.size(), 3);
        return w;
    };

    auto randomChoice = [&]() {
        BarChoice c;
        do c.tpl = rndInt(WAVE_TPL_COUNT); while (WAVE_TPL[c.tpl].needs16 && !allow16);
        c.gap = rndInt(GAP_COUNT);
        c.mini = miniOk && rnd() < 0.3;
        c.grav = gravOk && rnd() < 0.12;
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
            else c.grav = gravOk && !c.grav;
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
    for (size_t i = 0; i + 1 < ts.size(); i++) {
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
    walls2(si, ts2, ys2, gs2, tl.x(s.t0) + 75, std::max(tl.x(s.t0) + 75, wallEnd));
    used(s.mini ? "mini wave" : "wave");
}

} // namespace rg::gen
