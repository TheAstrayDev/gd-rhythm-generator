#include "Common.hpp"

// Ship, wave, ball and spider. All of them are "path-first": the intended player path is
// built from the music (keypoints on accents, toggles on onsets, flips on beats), then the
// corridor is carved around it. Musical accents pinch the corridor so the player feels
// the hit, long notes become straight tight flights.

namespace rg::gen {

// ---------------------------------------------------------------------------------------
// Corridor walls around a path, built from vertically scaled blocks
// ---------------------------------------------------------------------------------------

void Gen::walls2(size_t si, std::vector<double> const& ts, std::vector<double> const& ys,
                 std::vector<double> const& gaps, double xStart, double xEnd) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double sx0 = tl.x(s.t0);
    auto interp = [&](std::vector<double> const& vals, double t) {
        if (t <= ts.front()) return vals.front();
        if (t >= ts.back()) return vals.back();
        size_t i = size_t(std::upper_bound(ts.begin(), ts.end(), t) - ts.begin());
        double f = (t - ts[i - 1]) / (ts[i] - ts[i - 1]);
        return vals[i - 1] + (vals[i] - vals[i - 1]) * f;
    };
    WallGroups wg{nextGroup++, nextGroup++, si};
    walls.push_back(wg);
    for (double xc = xStart; xc <= xEnd; xc += 30) {
        double lo = 1e9, hi = -1e9, gap = 1e9;
        for (double px = xc - 15 - 6; px <= xc + 15 + 6; px += 3) {
            double t = s.t0 + (px - sx0) / v;
            double y = interp(ys, t);
            lo = std::min(lo, y), hi = std::max(hi, y);
            gap = std::min(gap, interp(gaps, t));
        }
        double bottomOfTop = std::ceil((hi + gap / 2) / 7.5) * 7.5;
        if (bottomOfTop < 330) {
            double h = 360 - bottomOfTop;
            out.add(obj::Block, xc, bottomOfTop + h / 2).kv(key::ScaleY, h / 30).group(wg.top);
        }
        double topOfBottom = std::floor((lo - gap / 2) / 7.5) * 7.5;
        if (topOfBottom >= 22.5) {
            out.add(obj::Block, xc, topOfBottom / 2).kv(key::ScaleY, topOfBottom / 30).group(wg.bottom);
        }
    }
}

// gap profile: base gap, pinched around accents, tightened during long notes
static double pinchAt(double t, std::vector<std::pair<double, double>> const& pinches) {
    double p = 0;
    for (auto& [pt, amount] : pinches) {
        double d = (t - pt) / 0.09;
        p = std::max(p, amount * std::exp(-d * d));
    }
    return p;
}

// ---------------------------------------------------------------------------------------
// Wave: toggles on onsets. Mini wave (63 degrees) in intense parts.
// ---------------------------------------------------------------------------------------

void Gen::genWave(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    const double ylo = 15, yhi = 210;
    double a = s.t0 + 0.45, b = s.t1 - 0.35;
    if (b <= a) return;
    int sub = diff() >= 4 ? 4 : 2;
    auto all = candidates(s, a, b, sub);
    // mini (63 degrees) is allowed per bar if it can still zigzag between eighths
    double step = sub >= 4 ? P / 4 : P / 2; // finest toggle spacing available
    bool miniOk = !s.mini && diff() >= 3 && s.level >= 2 && v * 2 * step <= 185;
    int nBars = std::max(1, int(std::ceil((s.t1 - s.t0) / (4 * P))));
    auto barOf = [&](double t) { return std::clamp(int((t - s.t0) / (4 * P)), 0, nBars - 1); };
    // a bar may go mini only if consecutive toggle candidates are close enough for the
    // steeper wave to turn around before hitting the ceiling
    std::vector<char> miniBarOk(size_t(nBars), miniOk ? 1 : 0);
    for (size_t i = 0; i + 1 < all.size(); i++) {
        if (all[i + 1].t - all[i].t > 175.0 / (2 * v)) {
            miniBarOk[size_t(barOf(all[i].t))] = 0;
            miniBarOk[size_t(barOf(all[i + 1].t))] = 0;
        }
    }
    // each wave part gets its own density: calm glide, normal, or spam on intense drops
    double dens = s.level >= 3 && diff() >= 3 && rnd() < 0.4 ? 1.7 : 0.8 + rnd() * 0.6;
    if (dens > 1.5) used("wave spam");
    double targetToggles = targetCount(s, a, b, 1.2 * dens);

    // A wave path is decided by which onsets toggle the direction (and, per bar, the size).
    // Search: a pool of random plans, then annealing on the best. Score: toggles on strong
    // onsets, varied amplitudes, size changes, no idling on the floor, density on target.
    struct SizeChange { double t, y; bool toMini; };
    struct WavePath { std::vector<double> ts, ys; std::vector<SizeChange> sizes; int toggles = 0; double score = -1e18; };
    struct Plan { std::vector<char> mask, mini; };
    auto build = [&](Plan const& pl) {
        WavePath w;
        w.ts = {s.t0}, w.ys = {ylo};
        bool curMini = s.mini;
        double vy = v * (curMini ? 2 : 1);
        double minGap = std::max(D.minGap, 0.1);
        double endZone = b - (yhi - ylo) / (v * 2) * 0.6;
        double t = s.t0, y = ylo, lastToggle = -1e9, floorIdle = 0, sync = 0;
        int dir = -1;
        std::vector<double> amps;
        for (size_t i = 0; i < all.size(); i++) {
            double ct = all[i].t;
            if (ct - lastToggle < std::max(minGap, curMini ? 0.12 : 0.1)) continue;
            double yc = std::clamp(y + dir * vy * (ct - t), ylo, yhi);
            double tn = i + 1 < all.size() ? all[i + 1].t : b;
            bool wantMini = miniOk ? bool(pl.mini[size_t(barOf(ct))] && miniBarOk[size_t(barOf(ct))]) : curMini;
            double vyNext = v * (wantMini ? 2 : 1);
            bool toggle = false;
            if (dir > 0) {
                if (yc + vy * (tn - ct) > yhi - 10 || ct > endZone) toggle = true;
                else if (pl.mask[i]) toggle = true;
            } else if (ct < endZone) {
                double floorSince = t + (y - ylo) / vy;
                bool idle = yc <= ylo + 1 && ct - floorSince > P;
                if (pl.mask[i] || idle) toggle = yc + vyNext * (tn - ct) <= yhi - 10;
            }
            if (!toggle) continue;
            if (dir < 0 && y - vy * (ct - t) < ylo) {
                double tf = t + (y - ylo) / vy;
                floorIdle += ct - tf;
                w.ts.push_back(tf), w.ys.push_back(ylo);
            }
            amps.push_back(std::abs(yc - y));
            sync += all[i].w;
            w.ts.push_back(ct), w.ys.push_back(yc);
            if (wantMini != curMini) {
                // the size portal sits on the toggle point, so every stretch has one slope
                w.sizes.push_back({ct, yc, wantMini});
                curMini = wantMini;
                vy = vyNext;
            }
            t = ct, y = yc, dir = -dir, lastToggle = ct;
            w.toggles++;
        }
        if (curMini != s.mini) w.sizes.push_back({t, y, s.mini}), vy = v * (s.mini ? 2 : 1);
        if (dir < 0) {
            double tf = t + (y - ylo) / vy;
            if (tf < s.t1) w.ts.push_back(tf), w.ys.push_back(ylo);
            w.ts.push_back(s.t1 + 1), w.ys.push_back(ylo);
        } else {
            double tf = t + (yhi - y) / vy;
            w.ts.push_back(tf), w.ys.push_back(yhi);
            w.ts.push_back(s.t1 + 1), w.ys.push_back(yhi);
        }
        double mean = 0, var = 0;
        for (double v2 : amps) mean += v2;
        mean /= std::max<size_t>(1, amps.size());
        for (double v2 : amps) var += (v2 - mean) * (v2 - mean);
        var = std::sqrt(var / std::max<size_t>(1, amps.size()));
        w.score = 2.0 * sync + var / 6.0 - 4.0 * floorIdle - 0.6 * std::abs(w.toggles - targetToggles)
                  + 3.0 * std::min<size_t>(w.sizes.size(), 4);
        return w;
    };
    auto randomPlan = [&](double p) {
        Plan pl;
        for (size_t i = 0; i < all.size(); i++) pl.mask.push_back(char(rnd() < p * (0.6 + all[i].w) ? 1 : 0));
        for (int i = 0; i < nBars; i++) pl.mini.push_back(char(miniOk && rnd() < 0.35 ? 1 : 0));
        return pl;
    };
    Plan bestPlan;
    WavePath best;
    {
        // seed: the strongest onsets, normal size
        auto want = pick(all, targetToggles, std::max(D.minGap, 0.1));
        std::set<double> wantT;
        for (auto& e : want) wantT.insert(e.t);
        for (size_t i = 0; i < all.size(); i++) bestPlan.mask.push_back(char(wantT.count(all[i].t) ? 1 : 0));
        bestPlan.mini.assign(size_t(nBars), 0);
        best = build(bestPlan);
    }
    double p = std::clamp(targetToggles / std::max<double>(1, all.size()), 0.1, 0.9);
    for (int cand = 0; cand < 20; cand++) {
        auto pl = randomPlan(p);
        auto w = build(pl);
        if (w.score > best.score) best = std::move(w), bestPlan = pl;
    }
    {
        Plan cur = bestPlan;
        double curScore = best.score;
        int rounds = 150, accepted = 0;
        for (int r = 0; r < rounds && !all.empty(); r++) {
            double temp = 2.0 * (1.0 - double(r) / rounds);
            Plan pl = cur;
            if (miniOk && rnd() < 0.2) pl.mini[size_t(rndInt(nBars))] ^= 1;
            else pl.mask[size_t(rndInt(int(all.size())))] ^= 1;
            if (rnd() < 0.3) pl.mask[size_t(rndInt(int(all.size())))] ^= 1;
            auto w = build(pl);
            if (w.score > curScore || rnd() < std::exp((w.score - curScore) / std::max(0.05, temp))) cur = pl, curScore = w.score;
            if (w.score > best.score) best = std::move(w), bestPlan = pl, accepted++;
        }
        features["wave improvement steps"] += accepted;
    }
    auto& ts = best.ts;
    auto& ys = best.ys;
    clicks += best.toggles;
    for (auto& sc : best.sizes) {
        out.add(sc.toMini ? obj::PortalSizeMini : obj::PortalSizeNormal, tl.x(sc.t) + 20, std::clamp(sc.y, 45.0, 190.0));
        used(sc.toMini ? "wave size switch (mini)" : "wave size switch (normal)");
    }
    auto miniAt = [&](double t) {
        bool m = s.mini;
        for (auto& sc : best.sizes) if (sc.t <= t) m = sc.toMini;
        return m;
    };

    // gap: phrases of different width, pinched on strong beats (never below a safe minimum)
    std::vector<double> phraseMul;
    for (int i = 0; i * 4 < nBars + 3; i++) {
        static constexpr double muls[3] = {0.85, 1.0, 1.25};
        phraseMul.push_back(muls[rndInt(3)]);
    }
    auto baseAt = [&](double t) {
        size_t ph = std::min(phraseMul.size() - 1, size_t(barOf(t) / 4));
        return D.waveGap * phraseMul[ph] * (miniAt(t) ? 0.8 : 1.0);
    };
    std::vector<std::pair<double, double>> pinches;
    for (size_t k = 0; k < beats.size(); k++) {
        if (beats[k] < s.t0 + 0.5 || beats[k] > s.t1 - 0.5) continue;
        float kick = kickAt(beats[k]);
        double base = baseAt(beats[k]), mg = miniAt(beats[k]) ? 34 : 44;
        if (kick > 0.55f && s.level >= 2) pinches.push_back({beats[k], (base - mg) * 0.5 * kick});
    }
    if (!pinches.empty()) used("wave pinch");
    std::vector<double> ts2, ys2, gs2;
    for (size_t i = 0; i + 1 < ts.size(); i++) {
        int n = std::max(1, int((ts[i + 1] - ts[i]) / 0.03));
        for (int k = 0; k < n; k++) {
            double tt = ts[i] + (ts[i + 1] - ts[i]) * k / n;
            ts2.push_back(tt);
            ys2.push_back(ys[i] + (ys[i + 1] - ys[i]) * double(k) / n);
            gs2.push_back(std::max(miniAt(tt) ? 34.0 : 44.0, baseAt(tt) - pinchAt(tt, pinches)));
        }
    }
    ts2.push_back(ts.back()), ys2.push_back(ys.back()), gs2.push_back(baseAt(ts.back()));
    walls2(si, ts2, ys2, gs2, tl.x(s.t0) + 75, tl.x(s.t1) - 30);
    used(s.mini ? "mini wave" : "wave");

    // gravity flips on downbeats (controls invert, the corridor stays valid)
    if (s.gravity) {
        std::vector<double> downs;
        for (size_t k = 0; k < beats.size(); k++)
            if (down[k] && beats[k] > s.t0 + 4 * P && beats[k] < s.t1 - 4 * P) downs.push_back(beats[k]);
        bool up = false;
        for (size_t k = 0; k < downs.size(); k += 2 + size_t(rndInt(2))) {
            double gt = downs[k];
            size_t idx = size_t(std::upper_bound(ts2.begin(), ts2.end(), gt) - ts2.begin());
            double gy = std::clamp(ys2[idx > 0 ? idx - 1 : 0], 30.0, 200.0);
            up = !up;
            out.add(up ? obj::PortalGravityUp : obj::PortalGravityDown, tl.x(gt), gy);
            used("gravity portal");
        }
        if (up) out.add(obj::PortalGravityDown, tl.x(s.t1) - 60, 45);
    }
}

// ---------------------------------------------------------------------------------------
// Ship: keypoints on accents with searched heights, two corridor styles (tunnel / gates),
// straight tight flights on long notes, size switches and gravity flips in intense parts
// ---------------------------------------------------------------------------------------

void Gen::genShip(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    double slope = diff() <= 1 ? 0.45 : 0.65;
    double a = s.t0 + 0.6, b = s.t1 - 0.8;
    std::vector<Cand> ev;
    if (b > a) {
        ev = pick(candidates(s, a, b, 1), (b - a) / D.shipSpacing, D.shipSpacing);
        // take off on the first beat instead of idling on the floor until the strongest hit
        if (ev.empty() || ev.front().t - a > P * 1.01) {
            for (double bt : beats) if (bt >= a) { ev.insert(ev.begin(), Cand{bt, 1.f, false}); break; }
            while (ev.size() > 1 && ev[1].t - ev[0].t < D.shipSpacing * 0.6) ev.erase(ev.begin() + 1);
        }
    }
    std::vector<float> sus;
    for (auto& e : ev) sus.push_back(sustainAt(e.t));

    // ---- keypoint heights: pool + annealing for big, varied moves on strong hits -------
    struct ShipPath { std::vector<double> kt, ky; std::vector<std::pair<double, double>> straight; double score = -1e18; };
    auto build = [&](std::vector<double> const& want) {
        ShipPath p;
        p.kt = {s.t0 + 0.3}, p.ky = {15};
        double score = 0;
        std::set<int> bins;
        for (size_t i = 0; i < ev.size(); i++) {
            double dt = ev[i].t - p.kt.back();
            double maxDy = slope * v * dt / 1.5708;
            double prev = p.ky.back();
            double target = std::clamp(want[i], prev - maxDy, prev + maxDy);
            score -= std::abs(target - want[i]) / 30.0; // asked for more than physics allows
            if (std::abs(target - prev) < 20) { score -= 1.5; continue; }
            score += ev[i].w * std::abs(target - prev) / 40.0; // big moves on strong hits
            bins.insert(int(target / 40));
            p.kt.push_back(ev[i].t), p.ky.push_back(target);
            if (sus[i] > 0.8 * P) {
                double hold = std::min(double(sus[i]), 2.0 * P);
                if (ev[i].t + hold < b && (i + 1 >= ev.size() || ev[i + 1].t > ev[i].t + hold)) {
                    p.kt.push_back(ev[i].t + hold), p.ky.push_back(target);
                    p.straight.push_back({ev[i].t, ev[i].t + hold});
                    score += 2.0;
                }
            }
        }
        p.score = score + 1.5 * bins.size();
        return p;
    };
    auto randomWant = [&]() {
        std::vector<double> w;
        for (size_t i = 0; i < ev.size(); i++) w.push_back(45 + rnd() * 155);
        return w;
    };
    std::vector<double> bestWant = randomWant();
    ShipPath best = build(bestWant);
    for (int i = 0; i < 20; i++) {
        auto w = randomWant();
        auto p = build(w);
        if (p.score > best.score) best = std::move(p), bestWant = w;
    }
    {
        auto cur = bestWant;
        double curScore = best.score;
        int accepted = 0;
        for (int r = 0; r < 120 && !ev.empty(); r++) {
            double temp = 2.0 * (1.0 - r / 120.0);
            auto w = cur;
            size_t k = size_t(rndInt(int(ev.size())));
            w[k] = std::clamp(w[k] + (rnd() - 0.5) * 120, 45.0, 200.0);
            auto p = build(w);
            if (p.score > curScore || rnd() < std::exp((p.score - curScore) / std::max(0.05, temp))) cur = w, curScore = p.score;
            if (p.score > best.score) best = std::move(p), bestWant = w, accepted++;
        }
        features["ship improvement steps"] += accepted;
    }
    auto kt = best.kt, ky = best.ky;
    auto& straight = best.straight;

    double tEnd = s.t1 - 0.15;
    // don't drift down to the floor over a long stretch: stay up, then land at the end
    if (tEnd - kt.back() > 2 * P) kt.push_back(tEnd - 1.2 * P), ky.push_back(90);
    kt.push_back(tEnd), ky.push_back(15);
    for (size_t i = ky.size() - 1; i-- > 1;) {
        double maxY = ky[i + 1] + slope * v * (kt[i + 1] - kt[i]) / 1.5708;
        ky[i] = std::min(ky[i], maxY);
    }

    // ---- corridor style ------------------------------------------------------------------
    bool gates = diff() >= 1 && rnd() < 0.5;
    used(gates ? "ship gates" : "ship tunnel");
    double minGapUnits = s.mini ? 55 : 70;
    double base = D.shipGap * (s.mini ? 0.85 : 1.0) * (gates ? 1.6 : 1.0);
    base = std::min(base, 230.0);
    std::vector<std::pair<double, double>> pinches;
    double width = gates ? 0.06 : 0.09;
    for (size_t k = 0; k < beats.size(); k++) {
        for (double t : {beats[k], beats[k] + P / 2}) {
            if (t < s.t0 + 0.6 || t > s.t1 - 0.6) continue;
            bool onBeat = t == beats[k];
            float hit = onBeat ? kickAt(t) : A.onsetAt(mt(t), 0.04);
            if (gates && hit > 0.5f && (onBeat || diff() >= 3)) pinches.push_back({t, (base - minGapUnits) * 0.75 * hit});
            else if (!gates && onBeat && hit > 0.6f && s.level >= 2) pinches.push_back({t, (base - minGapUnits) * 0.45 * hit});
        }
    }
    if (!pinches.empty()) used(gates ? "ship gate hits" : "ship pinch");
    auto pinch = [&](double t) {
        double p = 0;
        for (auto& [pt, amount] : pinches) {
            double d = (t - pt) / width;
            p = std::max(p, amount * std::exp(-d * d));
        }
        return p;
    };

    // ---- size switches every two bars in intense parts (path stays the same) -----------
    std::vector<std::pair<double, bool>> sizes;
    if (!s.mini && diff() >= 3 && s.level >= 2) {
        bool mini = false;
        for (size_t k = 0; k < beats.size(); k++) {
            if (!down[k] || beats[k] < s.t0 + 4 * P || beats[k] > s.t1 - 4 * P) continue;
            if (rnd() < 0.5) continue;
            mini = !mini;
            sizes.push_back({beats[k], mini});
        }
        if (mini) sizes.push_back({s.t1 - 2 * P, false});
    }
    auto miniAt = [&](double t) {
        bool m = s.mini;
        for (auto& [st, mm] : sizes) if (st <= t) m = mm;
        return m;
    };

    auto inStraight = [&](double t) {
        for (auto& [x0, x1] : straight) if (t >= x0 && t <= x1) return true;
        return false;
    };
    std::vector<double> ts, ys, gs;
    for (size_t i = 0; i + 1 < kt.size(); i++) {
        for (int k = 0; k < 16; k++) {
            double u = k / 16.0;
            double t = kt[i] + (kt[i + 1] - kt[i]) * u;
            ts.push_back(t);
            ys.push_back(ky[i] + (ky[i + 1] - ky[i]) * (1 - std::cos(3.14159265 * u)) / 2);
            double g = base * (miniAt(t) && !s.mini ? 0.85 : 1.0) - pinch(t);
            if (inStraight(t)) g = std::min(g, std::max(minGapUnits, D.shipGap * 0.7)); // straight fly
            gs.push_back(std::max(minGapUnits, g));
        }
    }
    if (!straight.empty()) used("ship straight fly");
    ts.push_back(kt.back()), ys.push_back(ky.back()), gs.push_back(base);
    ts.insert(ts.begin(), s.t0), ys.insert(ys.begin(), 15), gs.insert(gs.begin(), base);
    ts.push_back(s.t1 + 1), ys.push_back(15), gs.push_back(base);
    walls2(si, ts, ys, gs, tl.x(s.t0) + 90, tl.x(s.t1) - 30);
    clicks += int(kt.size());
    used(s.mini ? "mini ship" : "ship");

    auto yAt = [&](double t) {
        size_t i = size_t(std::upper_bound(ts.begin(), ts.end(), t) - ts.begin());
        return std::clamp(ys[i > 0 ? i - 1 : 0], 40.0, 190.0);
    };
    for (auto& [st, mm] : sizes) {
        out.add(mm ? obj::PortalSizeMini : obj::PortalSizeNormal, tl.x(st), yAt(st));
        used(mm ? "ship size switch (mini)" : "ship size switch (normal)");
    }
    if (s.gravity && kt.size() > 4) {
        size_t mid = kt.size() / 2;
        out.add(obj::PortalGravityUp, tl.x(kt[mid]), yAt(kt[mid]));
        out.add(obj::PortalGravityDown, tl.x(s.t1) - 60, 45);
        used("gravity portal");
    }
}

} // namespace rg::gen
