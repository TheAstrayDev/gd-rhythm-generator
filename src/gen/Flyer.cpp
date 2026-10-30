#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// UFO and swing: the "click flyers". Their path is a pure function of the click times, so the
// generator plans the clicks on the music and steps GD's own physics (240 steps per second,
// Common.hpp) to get the exact path, then carves the corridor around it.
//
//   ufo   : every click is a hop (~1.5 blocks); the altitude comes from how dense the clicks
//           are. The corridor pinches right under every click - the beat bounces you off the
//           floor - and opens up in between. Saws sit in the floor under some clicks.
//   swing : every click flips gravity, so the swing draws S-curves; the corridor follows them,
//           with saws on the outside of the turns.
//   both  : teleport portals throw the player to another lane on a downbeat (the corridor
//           splits there), the way modern levels switch lanes.
//
// The pilot: per bar a target altitude from a contour library (low, mid, high, rise, fall,
// swell) and a click appetite. At every 8th (16th on Demon) it simulates both "click" and
// "wait" a little ahead and takes the one that stays clear of the bounds and gets closer to
// the target, with a bonus for clicking on strong onsets. Search: a pool of per-bar choices,
// then annealing; score = clicks on the music, height variety, no scraping, density on target.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
struct Fly { double y, vy; int g; }; // swing gravity: -1 pulls down, +1 pulls up
enum Contour { FLow, FMid, FHigh, FRise, FFall, FSwell, F_COUNT };
struct FlyChoice { int contour = FMid; double appetite = 0.5; bool teleport = false; };
struct FlyPath {
    std::vector<double> ts, ys;    // every 1/60 s
    std::vector<double> clicks;
    std::vector<std::pair<double, double>> teleports; // (time, dy)
    std::vector<size_t> tpIdx;     // index of the first point after each teleport
    double score = -1e18;
};
constexpr double DT = 1.0 / 240;
} // namespace

void Gen::genFlyer(size_t si, bool swing) {
    auto& s = segs[si];
    double P = beatLen();
    double a = s.t0 + 0.5, b = s.t1 - 0.6;
    if (b <= a + P) return;
    const double ylo = 15, yhi = FLY_BOUND - 15;
    const double safeLo = 45, safeHi = 225;
    int dif = diff();
    bool allow16 = dif >= 4;
    double minGap = std::max(D.minGap, swing ? 0.16 : 0.12);
    bool tpOk = dif >= 2 && s.level >= 1;

    std::vector<double> bars;
    for (size_t k = 0; k < beats.size(); k++) if (down[k] && beats[k] >= s.t0 - 4 * P && beats[k] < b) bars.push_back(beats[k]);
    int nBars = int(bars.size());
    if (!nBars) return;
    struct CT { double t; float w; int bar; };
    std::vector<CT> cand;
    int sub = allow16 ? 4 : 2;
    for (size_t k = 0; k < beats.size(); k++) {
        for (int q = 0; q < sub; q++) {
            double t = beats[k] + q * P / sub;
            if (t < a || t > b) continue;
            float w = q == 0 ? std::max(0.5f, A.onsetAt(mt(t), 0.04)) : A.onsetAt(mt(t), 0.035) * (q == sub / 2 ? 1.f : 0.8f);
            int bi = 0;
            while (bi + 1 < nBars && bars[size_t(bi + 1)] <= t) bi++;
            cand.push_back({t, w, bi});
        }
    }
    std::sort(cand.begin(), cand.end(), [](auto& l, auto& r) { return l.t < r.t; });
    if (cand.empty()) return;
    double wantClicks = targetCount(s, a, b, swing ? 0.75 : 1.1);

    auto stepFly = [&](Fly& st, bool click) {
        if (swing) {
            if (click) st.g = -st.g, st.vy *= SWING_KEEP;
            st.vy = std::clamp(st.vy + st.g * SWING_G * DT, -SWING_MAX, SWING_MAX);
        } else {
            if (click) st.vy = std::max(st.vy, UFO_CLICK);
            st.vy = std::max(st.vy - (st.vy >= 103.5 ? UFO_G_FAST : UFO_G_SLOW) * DT, -UFO_FALL);
        }
        st.y += st.vy * DT;
        if (st.y < ylo) st.y = ylo, st.vy = std::max(st.vy, 0.0);
        if (st.y > yhi) st.y = yhi, st.vy = std::min(st.vy, 0.0);
    };
    auto targetOf = [&](FlyChoice const& c, double t, int bi) {
        double u = std::clamp((t - bars[size_t(bi)]) / (4 * P), 0.0, 1.0);
        switch (c.contour) {
            case FLow: return 75.0;
            case FHigh: return 195.0;
            case FRise: return 70 + 130 * u;
            case FFall: return 200 - 130 * u;
            case FSwell: return 135 + 60 * std::sin(u * 6.2832);
            default: return 135.0;
        }
    };

    auto build = [&](std::vector<FlyChoice> const& ch) {
        FlyPath p;
        Fly st{ylo, 0, -1};
        double t = s.t0, lastClick = -1e9, sync = 0, scrape = 0;
        std::set<int> bins;
        int prevBar = -1;
        auto advance = [&](double until) {
            while (t + DT <= until) {
                stepFly(st, false);
                t += DT;
                if (st.y < safeLo - 20 || st.y > safeHi + 20) scrape += DT;
                if (std::fmod(t - s.t0, 1.0 / 60) < DT) p.ts.push_back(t), p.ys.push_back(st.y);
            }
        };
        p.ts.push_back(t), p.ys.push_back(st.y);
        for (size_t k = 0; k < cand.size(); k++) {
            auto& c = cand[k];
            advance(c.t);
            auto& fc = ch[size_t(c.bar)];
            // teleport on the first beat of a bar that asks for it
            if (c.bar != prevBar) {
                prevBar = c.bar;
                if (fc.teleport && tpOk && c.t > a + 2 * P && c.t < b - 4 * P && std::abs(c.t - bars[size_t(c.bar)]) < 0.01 &&
                    !nearSpeedNet(c.t, P)) {
                    double dy = st.y < 135 ? 105 + 30 * fc.appetite : -(105 + 30 * fc.appetite);
                    if (st.y + dy > safeLo + 10 && st.y + dy < safeHi - 10) {
                        p.teleports.push_back({t, dy});
                        p.tpIdx.push_back(p.ts.size());
                        st.y += dy;
                        p.ts.push_back(t), p.ys.push_back(st.y);
                    }
                }
            }
            if (c.t - lastClick < minGap) continue;
            double target = targetOf(fc, c.t, c.bar);
            double horizon = std::min(swing ? 0.5 : 0.35, (k + 1 < cand.size() ? cand[k + 1].t - c.t : 0.3) + 0.2);
            auto predict = [&](bool click, double& lo, double& hi) {
                Fly q = st;
                lo = hi = q.y;
                for (double d = 0; d < horizon; d += DT) {
                    stepFly(q, click && d == 0);
                    lo = std::min(lo, q.y), hi = std::max(hi, q.y);
                }
                return q.y;
            };
            double lo1, hi1, lo0, hi0;
            double y1 = predict(true, lo1, hi1), y0 = predict(false, lo0, hi0);
            auto cost = [&](double ye, double lo, double hi) {
                return std::abs(ye - target) / 30 + 4.0 * (std::max(0.0, safeLo - lo) + std::max(0.0, hi - safeHi)) / 30;
            };
            double cClick = cost(y1, lo1, hi1) - 1.2 * c.w - 0.6 * (fc.appetite - 0.5);
            double cWait = cost(y0, lo0, hi0);
            if (cClick < cWait) {
                stepFly(st, true);
                t += DT;
                p.ts.push_back(t), p.ys.push_back(st.y);
                p.clicks.push_back(c.t);
                lastClick = c.t;
                sync += c.w;
                bins.insert(int(st.y / 45));
            }
        }
        advance(s.t1 - 0.3);
        double nC = double(p.clicks.size());
        std::set<int> contours;
        for (auto& c : ch) contours.insert(c.contour);
        p.score = 2.0 * sync - 1.0 * std::abs(nC - wantClicks) + 3.0 * double(bins.size()) + 1.5 * double(contours.size())
                  - 6.0 * scrape / P + 3.0 * std::min<size_t>(p.teleports.size(), 3) * (s.g.teleports - 0.2);
        return p;
    };

    auto randomChoice = [&]() {
        FlyChoice c;
        c.contour = rndInt(F_COUNT);
        c.appetite = rnd();
        c.teleport = tpOk && rnd() < 0.1 + 0.25 * s.g.teleports;
        return c;
    };
    std::vector<FlyChoice> bestCh;
    FlyPath best;
    for (int cnd = 0; cnd < 16; cnd++) {
        std::vector<FlyChoice> ch;
        for (int i = 0; i < nBars; i++) ch.push_back(randomChoice());
        auto p = build(ch);
        if (p.score > best.score) best = std::move(p), bestCh = ch;
    }
    {
        auto cur = bestCh;
        double curScore = best.score;
        int accepted = 0, rounds = 80;
        for (int r = 0; r < rounds; r++) {
            double temp = 2.0 * (1.0 - double(r) / rounds);
            auto ch = cur;
            auto& c = ch[size_t(rndInt(nBars))];
            double m = rnd();
            if (m < 0.5) c.contour = rndInt(F_COUNT);
            else if (m < 0.85) c.appetite = rnd();
            else c.teleport = tpOk && !c.teleport;
            auto p = build(ch);
            if (p.score > curScore || rnd() < std::exp((p.score - curScore) / std::max(0.05, temp))) cur = ch, curScore = p.score;
            if (p.score > best.score) best = std::move(p), bestCh = ch, accepted++;
        }
        features[swing ? "swing improvement steps" : "ufo improvement steps"] += accepted;
    }
    clicks += int(best.clicks.size());

    // ---- corridor: one lane per stretch between teleports ------------------------------------
    double base = std::min(230.0, D.shipGap * (swing ? 1.1 : 1.2));
    double tight = D.shipGap * (swing ? 0.8 : 0.85);
    auto gapAt = [&](double t) {
        double p = 0;
        for (double c : best.clicks) {
            if (std::abs(t - c) > 0.3) continue;
            double strength = A.onsetAt(mt(c), 0.04) > 0.5f ? 1.0 : 0.6;
            double d = (t - c) / (swing ? 0.08 : 0.06);
            p = std::max(p, (base - tight) * strength * std::exp(-d * d));
        }
        return base - p;
    };
    std::vector<double> gs, dys;
    for (double t : best.ts) gs.push_back(gapAt(t));
    for (auto& tp : best.teleports) dys.push_back(tp.second);
    auto lanes = splitAtTeleports(best.ts, best.ys, gs, best.tpIdx, dys, PLAYER_HALF);
    // saws: under some ufo clicks (bottom wall), on the outside of swing turns
    int sawSize = s.g.saws > 0.66 ? 2 : s.g.saws > 0.33 ? 1 : 0;
    bool blades = rnd() < 0.5;
    auto sawSpec = (blades ? BLADES : SAWS)[sawSize];
    if (dif >= 1) {
        for (size_t k = 0; k < best.clicks.size(); k++) {
            if (rnd() > 0.15 + 0.35 * s.g.saws) continue;
            double c = best.clicks[k];
            bool top = false;
            if (swing) {
                // the turn: where the swing was heading before the flip
                size_t i = size_t(std::lower_bound(best.ts.begin(), best.ts.end(), c) - best.ts.begin());
                if (i < 2 || i >= best.ys.size()) continue;
                top = best.ys[i - 1] > best.ys[i - 2];
            }
            for (auto& L : lanes) if (!L.ts.empty() && c > L.ts.front() + 0.1 && c < L.ts.back() - 0.1) {
                L.bites.push_back({c + (swing ? 0.12 : 0.0), top, sawSpec.first, sawSpec.second});
            }
        }
    }
    corridor(si, lanes, tl.x(s.t0) + 90, tl.x(s.t1) - 30);
    for (size_t k = 0; k < best.teleports.size(); k++) {
        auto [t, dy] = best.teleports[k];
        size_t i = best.tpIdx[k];
        double y = best.ys[i > 0 ? i - 1 : 0];
        out.add(obj::Teleport, tl.x(t), std::clamp(y, 45.0, 255.0)).kv(key::TeleportOffset, dy);
        used("teleport lane switch");
    }
    used(swing ? "swing" : "ufo");
}

} // namespace rg::gen
