#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Ship: a library of 18 per-bar flight templates, each with its own path shape and
// corridor style, plus per-bar size and gravity modifiers.
//
// A template is a list of moves on beat positions (up / down / hold / top / bottom / mid /
// random level). The path is built through slope-limited keypoints with smooth easing;
// asking for more than the ship can fly is penalised. Search: pool of bar sequences, then
// annealing; score = template variety, no repeats, moves on real onsets, use of the whole
// height, size / gravity changes, few physics clamps.
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
enum MoveKind { MUp, MDown, MHold, MTop, MBottom, MMid, MRand };
struct Move { double beat; MoveKind kind; };
enum Corr { CNormal, CTight, CWide, CGates, CTeeth, CSqueeze };
struct ShipTpl { char const* name; std::vector<Move> moves; double amp; Corr corr; bool follow = false, echo = false; };

std::vector<ShipTpl> const& shipTemplates() {
    static const std::vector<ShipTpl> t = {
        {"ship waves", {{0, MUp}, {2, MDown}}, 1.0, CNormal},
        {"ship zigzag", {{0, MUp}, {0.5, MDown}, {1, MUp}, {1.5, MDown}, {2, MUp}, {2.5, MDown}, {3, MUp}, {3.5, MDown}}, 0.35, CNormal},
        {"ship terraces", {{0, MUp}, {1, MHold}, {2, MDown}, {3, MHold}}, 0.8, CNormal},
        {"ship climb", {{0, MUp}, {1, MUp}, {2, MUp}, {3, MUp}}, 0.4, CNormal},
        {"ship dive", {{0, MDown}, {1, MDown}, {2, MDown}, {3, MDown}}, 0.4, CNormal},
        {"ship straight fly", {{0, MMid}, {4, MHold}}, 0.0, CTight},
        {"ship long swell", {{0, MTop}, {2, MBottom}}, 1.0, CWide},
        {"ship double bump", {{0, MUp}, {0.5, MDown}, {1, MUp}, {1.5, MDown}, {2, MHold}}, 0.5, CNormal},
        {"ship gates", {{0, MUp}, {2, MDown}}, 0.9, CGates},
        {"ship squeeze", {{0, MMid}, {4, MHold}}, 0.0, CSqueeze},
        {"ship open air", {{0, MRand}, {2, MRand}}, 1.0, CWide},
        {"ship M", {{0, MTop}, {1, MMid}, {2, MTop}, {3, MBottom}}, 1.0, CNormal},
        {"ship sawtooth", {{0, MBottom}, {3, MTop}, {3.5, MMid}}, 1.0, CNormal},
        {"ship offbeat dodge", {{0.5, MUp}, {1.5, MDown}, {2.5, MUp}, {3.5, MDown}}, 0.6, CTeeth},
        {"ship steps", {{0, MDown}, {1, MHold}, {2, MUp}, {3, MHold}}, 0.7, CNormal},
        {"ship pulse gates", {{0, MHold}, {4, MHold}}, 0.0, CGates},
        {"ship echo", {}, 0.0, CNormal, false, true},
        {"ship follow the music", {}, 0.8, CNormal, true, false},
    };
    return t;
}

struct BarChoice { int tpl = 0; bool mini = false, grav = false; double ampMul = 1.0; };
} // namespace

void Gen::genShip(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    double slope = diff() <= 1 ? 0.45 : 0.65;
    double a = s.t0 + 0.6, b = s.t1 - 0.8;
    auto& T = shipTemplates();
    int nT = int(T.size());
    std::vector<double> bars;
    for (size_t k = 0; k < beats.size(); k++) if (down[k] && beats[k] >= s.t0 - 4 * P && beats[k] < b) bars.push_back(beats[k]);
    int nBars = int(bars.size());
    bool miniOk = !s.mini && diff() >= 3 && s.level >= 2;
    bool gravOk = diff() >= 2 && s.level >= 2;
    const double yMin = 45, yMax = 200, yMid = 115;

    struct Path {
        std::vector<double> kt, ky;
        std::vector<std::pair<double, double>> holds; // straight stretches
        std::vector<std::pair<double, bool>> sizes, gravs;
        double score = -1e18;
    };
    auto build = [&](std::vector<BarChoice> const& ch) {
        Path p;
        p.kt = {s.t0 + 0.3}, p.ky = {15};
        double score = 0, clamp = 0, sync = 0;
        std::set<int> usedT, bins;
        int repeats = 0;
        bool mini = s.mini, grav = false;
        double h = 15;
        auto push = [&](double t, double want, bool hold) {
            if (t <= p.kt.back() + 0.08 || t < a || t > b) return;
            double dt = t - p.kt.back();
            double maxDy = slope * v * dt / 1.5708;
            double y = std::clamp(want, h - maxDy, h + maxDy);
            y = std::clamp(y, yMin, yMax);
            clamp += std::abs(y - std::clamp(want, yMin, yMax)) / 30.0;
            if (hold) p.holds.push_back({p.kt.back(), t});
            else sync += A.onsetAt(mt(t), 0.04) * std::abs(y - h) / 60.0;
            p.kt.push_back(t), p.ky.push_back(y);
            bins.insert(int(y / 40));
            h = y;
        };
        // take off on the first beat of the part, whatever the first template does
        for (double bt : beats) if (bt >= a) { push(bt, yMid, false); break; }
        for (int i = 0; i < nBars; i++) {
            auto c = ch[size_t(i)];
            int ti = c.tpl;
            if (T[size_t(ti)].echo && i > 0) ti = ch[size_t(i - 1)].tpl;
            if (T[size_t(ti)].echo) ti = 0;
            usedT.insert(c.tpl);
            if (i > 0 && ch[size_t(i - 1)].tpl == c.tpl) repeats++;
            double bt = bars[size_t(i)];
            if (miniOk && c.mini != mini && bt > a && bt < b - 4 * P) mini = c.mini, p.sizes.push_back({bt, mini});
            if (gravOk && c.grav && bt > a + P && bt < b - 4 * P) grav = !grav, p.gravs.push_back({bt, grav});
            auto& tp = T[size_t(ti)];
            double amp = 125 * tp.amp * c.ampMul;
            if (tp.follow) {
                int dir = h > yMid ? -1 : 1;
                for (int k = 0; k < 8; k++) {
                    double t = bt + k * P / 2;
                    if (A.onsetAt(mt(t), 0.04) < 0.4f) continue;
                    push(t, h + dir * amp * (0.5 + 0.5 * A.onsetAt(mt(t), 0.04)), false);
                    dir = -dir;
                }
                continue;
            }
            for (auto& m : tp.moves) {
                double t = bt + m.beat * P;
                double want = h;
                switch (m.kind) {
                    case MUp: want = h + amp; break;
                    case MDown: want = h - amp; break;
                    case MHold: want = h; break;
                    case MTop: want = yMax - 10; break;
                    case MBottom: want = yMin + 10; break;
                    case MMid: want = yMid; break;
                    case MRand: { // deterministic "random" level from the bar's amplitude choice
                        double f = (c.ampMul - 0.7) / 0.6;
                        want = m.beat < 1 ? yMin + f * (yMax - yMin) : yMax - f * (yMax - yMin);
                        break;
                    }
                }
                // bounce off the limits instead of pushing into them
                if (want > yMax) want = h - amp * 0.6;
                if (want < yMin) want = h + amp * 0.6;
                push(t, want, m.kind == MHold);
            }
        }
        if (mini != s.mini) p.sizes.push_back({b - 2 * P, s.mini});
        if (grav) p.gravs.push_back({b - 2 * P, false});
        double gateBias = 0;
        for (auto& c : ch) {
            Corr k = T[size_t(c.tpl)].corr;
            if (k == CGates || k == CTeeth || k == CSqueeze) gateBias += 1;
        }
        score = 3.0 * usedT.size() - 4.0 * repeats + 2.0 * sync - 1.5 * clamp + 3.0 * bins.size()
                + 3.0 * (s.g.shipGates - 0.5) * gateBias
                + 2.0 * std::min<size_t>(p.sizes.size(), 4) + 2.0 * std::min<size_t>(p.gravs.size(), 3);
        p.score = score;
        return p;
    };
    auto randomChoice = [&]() {
        BarChoice c;
        c.tpl = rndInt(nT);
        c.mini = miniOk && rnd() < 0.3;
        c.grav = gravOk && rnd() < 0.12;
        c.ampMul = 0.7 + rnd() * 0.6;
        return c;
    };
    std::vector<BarChoice> bestCh;
    Path best;
    for (int cand = 0; cand < 24 && nBars > 0; cand++) {
        std::vector<BarChoice> ch;
        for (int i = 0; i < nBars; i++) ch.push_back(randomChoice());
        auto p = build(ch);
        if (p.score > best.score) best = std::move(p), bestCh = ch;
    }
    if (nBars > 0) {
        auto cur = bestCh;
        double curScore = best.score;
        int accepted = 0, rounds = 200;
        for (int r = 0; r < rounds; r++) {
            double temp = 2.5 * (1.0 - double(r) / rounds);
            auto ch = cur;
            auto& c = ch[size_t(rndInt(nBars))];
            double m = rnd();
            if (m < 0.6) c.tpl = rndInt(nT);
            else if (m < 0.8) c.ampMul = 0.7 + rnd() * 0.6;
            else if (m < 0.92) c.mini = miniOk && !c.mini;
            else c.grav = gravOk && !c.grav;
            auto p = build(ch);
            if (p.score > curScore || rnd() < std::exp((p.score - curScore) / std::max(0.05, temp))) cur = ch, curScore = p.score;
            if (p.score > best.score) best = std::move(p), bestCh = ch, accepted++;
        }
        features["ship improvement steps"] += accepted;
    } else {
        best = build({});
    }
    for (auto& c : bestCh) used(T[size_t(c.tpl)].name);

    auto kt = best.kt, ky = best.ky;
    double tEnd = s.t1 - 0.15;
    if (tEnd - kt.back() > 2 * P) kt.push_back(tEnd - 1.2 * P), ky.push_back(90);
    kt.push_back(tEnd), ky.push_back(15);
    for (size_t i = ky.size() - 1; i-- > 1;) {
        double maxY = ky[i + 1] + slope * v * (kt[i + 1] - kt[i]) / 1.5708;
        ky[i] = std::min(ky[i], maxY);
    }

    // ---- corridor per bar ----------------------------------------------------------------
    auto barIdx = [&](double t) {
        int i = 0;
        while (i + 1 < nBars && bars[size_t(i + 1)] <= t) i++;
        return i;
    };
    auto corrAt = [&](double t) { return nBars ? T[size_t(bestCh[size_t(barIdx(t))].tpl)].corr : CNormal; };
    auto miniAt = [&](double t) {
        bool m = s.mini;
        for (auto& [st, mm] : best.sizes) if (st <= t) m = mm;
        return m;
    };
    double minGapUnits = 70;
    std::vector<std::pair<double, double>> pinches; // (time, amount), for gates / teeth / pulses
    for (size_t k = 0; k < beats.size(); k++) {
        for (double t : {beats[k], beats[k] + P / 2}) {
            if (t < s.t0 + 0.6 || t > s.t1 - 0.6) continue;
            Corr c = corrAt(t);
            bool onBeat = t == beats[k];
            float hit = onBeat ? std::max(kickAt(t), 0.5f) : A.onsetAt(mt(t), 0.04);
            if (c == CGates && onBeat) pinches.push_back({t, 0.75 * hit});
            else if (c == CTeeth && hit > 0.4f) pinches.push_back({t, 0.55 * hit});
            else if (c == CNormal && onBeat && kickAt(t) > 0.6f && s.level >= 2) pinches.push_back({t, 0.35 * kickAt(t)});
        }
    }
    auto gapAt = [&](double t) {
        Corr c = corrAt(t);
        double base = D.shipGap;
        if (c == CTight) base *= 0.75;
        else if (c == CWide) base *= 1.45;
        else if (c == CGates) base *= 1.6;
        else if (c == CTeeth) base *= 1.25;
        else if (c == CSqueeze && nBars) {
            int bi = barIdx(t);
            double u = std::clamp((t - bars[size_t(bi)]) / (4 * P), 0.0, 1.0);
            base *= 1.4 - 0.65 * u; // the tunnel closes in over the bar
        }
        if (miniAt(t)) base *= 0.85;
        base = std::min(base, 230.0);
        double p = 0;
        for (auto& [pt, amount] : pinches) {
            double d = (t - pt) / 0.06;
            p = std::max(p, (base - minGapUnits) * amount * std::exp(-d * d));
        }
        return std::max(minGapUnits * (miniAt(t) ? 0.8 : 1.0), base - p);
    };
    std::vector<double> ts, ys, gs;
    for (size_t i = 0; i + 1 < kt.size(); i++) {
        for (int k = 0; k < 16; k++) {
            double u = k / 16.0;
            double t = kt[i] + (kt[i + 1] - kt[i]) * u;
            ts.push_back(t);
            ys.push_back(ky[i] + (ky[i + 1] - ky[i]) * (1 - std::cos(3.14159265 * u)) / 2);
            gs.push_back(gapAt(t));
        }
    }
    ts.push_back(kt.back()), ys.push_back(ky.back()), gs.push_back(D.shipGap);
    ts.insert(ts.begin(), s.t0), ys.insert(ys.begin(), 15), gs.insert(gs.begin(), D.shipGap);
    ts.push_back(s.t1 + 1), ys.push_back(15), gs.push_back(D.shipGap);
    walls2(si, ts, ys, gs, tl.x(s.t0) + 90, tl.x(s.t1) - 30);
    clicks += int(kt.size());
    used(s.mini ? "mini ship" : "ship");

    auto yAt = [&](double t) {
        size_t i = size_t(std::upper_bound(ts.begin(), ts.end(), t) - ts.begin());
        return std::clamp(ys[i > 0 ? i - 1 : 0], 40.0, 190.0);
    };
    for (auto& [st, mm] : best.sizes) {
        out.add(mm ? obj::PortalSizeMini : obj::PortalSizeNormal, tl.x(st), yAt(st));
        used(mm ? "ship size switch (mini)" : "ship size switch (normal)");
    }
    for (auto& [st, up] : best.gravs) {
        out.add(up ? obj::PortalGravityUp : obj::PortalGravityDown, tl.x(st), yAt(st));
        used("gravity portal");
    }
    if (s.gravity && best.gravs.empty() && kt.size() > 4) {
        size_t mid = kt.size() / 2;
        out.add(obj::PortalGravityUp, tl.x(kt[mid]), yAt(kt[mid]));
        out.add(obj::PortalGravityDown, tl.x(s.t1) - 60, 45);
        used("gravity portal");
    }
}

} // namespace rg::gen
