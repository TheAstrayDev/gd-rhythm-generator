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
    double slope = s.mini ? 2.0 : 1.0;
    double vy = v * slope;
    const double ylo = 15, yhi = 210;
    double a = s.t0 + 0.45, b = s.t1 - 0.35;
    if (b <= a) return;
    int sub = diff() >= 4 ? 4 : 2;
    auto all = candidates(s, a, b, sub);
    double minGap = std::max(D.minGap, s.mini ? 0.12 : 0.1);
    // A wave path is decided by which onsets toggle the direction. Search: a pool of random
    // toggle masks, then annealing on the best one. Score: toggles on strong onsets, varied
    // amplitudes (not the same zigzag every beat), no idling on the floor, density close to
    // the difficulty target.
    // each wave part gets its own density: calm glide, normal, or spam on intense drops
    double dens = s.level >= 3 && diff() >= 3 && rnd() < 0.4 ? 1.7 : 0.8 + rnd() * 0.6;
    if (dens > 1.5) used("wave spam");
    double targetToggles = targetCount(s, a, b, 1.2 * dens);
    double endZone = b - (yhi - ylo) / vy * 0.6;
    struct WavePath { std::vector<double> ts, ys; int toggles = 0; double score = -1e18; };
    auto build = [&](std::vector<char> const& mask) {
        WavePath w;
        w.ts = {s.t0}, w.ys = {ylo};
        double t = s.t0, y = ylo, lastToggle = -1e9, floorIdle = 0, sync = 0;
        int dir = -1;
        std::vector<double> amps;
        auto adv = [&](double from, double yy, int d, double to) { return std::clamp(yy + d * vy * (to - from), ylo, yhi); };
        for (size_t i = 0; i < all.size(); i++) {
            double ct = all[i].t;
            if (ct - lastToggle < minGap) continue;
            double yc = adv(t, y, dir, ct);
            double tn = i + 1 < all.size() ? all[i + 1].t : b;
            bool toggle = false;
            if (dir > 0) {
                if (yc + vy * (tn - ct) > yhi - 10 || ct > endZone) toggle = true;
                else if (mask[i]) toggle = true;
            } else if (ct < endZone) {
                double floorSince = t + (y - ylo) / vy;
                bool idle = yc <= ylo + 1 && ct - floorSince > beatLen();
                if (mask[i] || idle) toggle = yc + vy * (tn - ct) <= yhi - 10;
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
            t = ct, y = yc, dir = -dir, lastToggle = ct;
            w.toggles++;
        }
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
        w.score = 2.0 * sync + var / 6.0 - 4.0 * floorIdle - 0.6 * std::abs(w.toggles - targetToggles);
        return w;
    };
    WavePath best;
    std::vector<char> bestMask(all.size(), 0);
    auto want = pick(all, targetToggles, minGap);
    {
        // seed candidate: the strongest onsets
        std::set<double> wantT;
        for (auto& e : want) wantT.insert(e.t);
        for (size_t i = 0; i < all.size(); i++) bestMask[i] = char(wantT.count(all[i].t) ? 1 : 0);
        best = build(bestMask);
    }
    double p = std::clamp(targetToggles / std::max<double>(1, all.size()), 0.1, 0.9);
    for (int cand = 0; cand < 20; cand++) {
        std::vector<char> m(all.size());
        for (size_t i = 0; i < all.size(); i++) m[i] = char(rnd() < p * (0.6 + all[i].w) ? 1 : 0);
        auto w = build(m);
        if (w.score > best.score) best = std::move(w), bestMask = m;
    }
    {
        std::vector<char> cur = bestMask;
        double curScore = best.score;
        int rounds = 150, accepted = 0;
        for (int r = 0; r < rounds && !all.empty(); r++) {
            double temp = 2.0 * (1.0 - double(r) / rounds);
            std::vector<char> m = cur;
            m[size_t(rndInt(int(all.size())))] ^= 1;
            if (rnd() < 0.3) m[size_t(rndInt(int(all.size())))] ^= 1;
            auto w = build(m);
            if (w.score > curScore || rnd() < std::exp((w.score - curScore) / std::max(0.05, temp))) cur = m, curScore = w.score;
            if (w.score > best.score) best = std::move(w), bestMask = m, accepted++;
        }
        features["wave improvement steps"] += accepted;
    }
    std::vector<double> ts = best.ts, ys = best.ys;
    clicks += best.toggles;

    // gap: pinch on strong beats (never below a safe minimum)
    double base = D.waveGap * (s.mini ? 0.8 : 1.0);
    double minGapUnits = s.mini ? 34 : 44;
    std::vector<std::pair<double, double>> pinches;
    for (size_t k = 0; k < beats.size(); k++) {
        if (beats[k] < s.t0 + 0.5 || beats[k] > s.t1 - 0.5) continue;
        float kick = kickAt(beats[k]);
        if (kick > 0.55f && s.level >= 2) pinches.push_back({beats[k], (base - minGapUnits) * 0.5 * kick});
    }
    if (!pinches.empty()) used("wave pinch");
    std::vector<double> gaps;
    for (double tt : ts) gaps.push_back(std::max(minGapUnits, base - pinchAt(tt, pinches)));
    // denser sampling so pinches show up between path vertices
    std::vector<double> ts2, ys2, gs2;
    for (size_t i = 0; i + 1 < ts.size(); i++) {
        int n = std::max(1, int((ts[i + 1] - ts[i]) / 0.03));
        for (int k = 0; k < n; k++) {
            double tt = ts[i] + (ts[i + 1] - ts[i]) * k / n;
            ts2.push_back(tt);
            ys2.push_back(ys[i] + (ys[i + 1] - ys[i]) * double(k) / n);
            gs2.push_back(std::max(minGapUnits, base - pinchAt(tt, pinches)));
        }
    }
    ts2.push_back(ts.back()), ys2.push_back(ys.back()), gs2.push_back(base);
    walls2(si, ts2, ys2, gs2, tl.x(s.t0) + 75, tl.x(s.t1) - 30);
    used(s.mini ? "mini wave" : "wave");

    // gravity flip in the middle of the part (controls invert, the corridor stays valid)
    if (s.gravity) {
        double tm = (s.t0 + s.t1) / 2;
        int bi = 0;
        for (size_t k = 0; k < beats.size(); k++) if (down[k] && beats[k] >= tm) { bi = int(k); break; }
        double gx = tl.x(beats[size_t(bi)]);
        double gy = std::clamp(ys2[size_t(std::upper_bound(ts2.begin(), ts2.end(), beats[size_t(bi)]) - ts2.begin()) - 1], 30.0, 200.0);
        out.add(obj::PortalGravityUp, gx, gy);
        out.add(obj::PortalGravityDown, tl.x(s.t1) - 60, 45);
        used("gravity portal");
    }
}

// ---------------------------------------------------------------------------------------
// Ship: smooth path with turns on accents, straight tight flights on long notes
// ---------------------------------------------------------------------------------------

void Gen::genShip(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double slope = diff() <= 1 ? 0.45 : 0.65;
    double a = s.t0 + 0.6, b = s.t1 - 0.8;
    std::vector<double> kt{s.t0 + 0.3}, ky{15};
    std::vector<std::pair<double, double>> straight; // (start, end) of held notes
    if (b > a) {
        auto ev = pick(candidates(s, a, b, 1), (b - a) / D.shipSpacing, D.shipSpacing);
        // take off on the first beat instead of idling on the floor until the strongest hit
        if (ev.empty() || ev.front().t - a > beatLen() * 1.01) {
            for (double bt : beats) if (bt >= a) { ev.insert(ev.begin(), Cand{bt, 1.f, false}); break; }
            while (ev.size() > 1 && ev[1].t - ev[0].t < D.shipSpacing * 0.6) ev.erase(ev.begin() + 1);
        }
        int dir = 1;
        // ship "personality" for this part: wide waves, tight zigzag or terraces
        int shape = rndInt(3);
        used(shape == 0 ? "ship waves" : shape == 1 ? "ship zigzag" : "ship terraces");
        for (auto& e : ev) {
            double dt = e.t - kt.back();
            double maxDy = slope * v * dt / 1.5708;
            double mag = shape == 0 ? 60 + rnd() * 90 : shape == 1 ? 35 + rnd() * 40 : 50 + rnd() * 60;
            double target = std::clamp(ky.back() + dir * std::min(mag, maxDy), 45.0, 200.0);
            if (std::abs(target - ky.back()) < 20) { dir = -dir; continue; }
            float sus = sustainAt(e.t);
            if (shape == 2 || sus > 0.8 * beatLen()) {
                // terrace / held note: fly straight after reaching the height
                double hold = std::min(double(sus > 0 ? sus : beatLen()), 2.0 * beatLen());
                target = std::max(target, 70.0); // straight flights in the open, not along the floor
                kt.push_back(e.t), ky.push_back(target);
                if (e.t + hold < b) {
                    kt.push_back(e.t + hold), ky.push_back(target);
                    straight.push_back({e.t, e.t + hold});
                }
            } else {
                kt.push_back(e.t), ky.push_back(target);
            }
            dir = -dir;
        }
    }
    double tEnd = s.t1 - 0.15;
    // don't drift down to the floor over a long stretch: stay up, then land at the end
    if (tEnd - kt.back() > 2 * beatLen()) kt.push_back(tEnd - 1.2 * beatLen()), ky.push_back(90);
    kt.push_back(tEnd), ky.push_back(15);
    for (size_t i = ky.size() - 1; i-- > 1;) {
        double maxY = ky[i + 1] + slope * v * (kt[i + 1] - kt[i]) / 1.5708;
        ky[i] = std::min(ky[i], maxY);
    }
    std::vector<double> ts, ys, gs;
    double base = D.shipGap * (s.mini ? 0.85 : 1.0);
    double minGapUnits = s.mini ? 55 : 70;
    std::vector<std::pair<double, double>> pinches;
    for (size_t k = 0; k < beats.size(); k++) {
        if (beats[k] < s.t0 + 0.6 || beats[k] > s.t1 - 0.6) continue;
        float kick = kickAt(beats[k]);
        if (kick > 0.6f && s.level >= 2) pinches.push_back({beats[k], (base - minGapUnits) * 0.45 * kick});
    }
    if (!pinches.empty()) used("ship pinch");
    auto inStraight = [&](double t) {
        for (auto& [x0, x1] : straight) if (t >= x0 && t <= x1) return true;
        return false;
    };
    for (size_t i = 0; i + 1 < kt.size(); i++) {
        for (int k = 0; k < 16; k++) {
            double u = k / 16.0;
            double t = kt[i] + (kt[i + 1] - kt[i]) * u;
            ts.push_back(t);
            ys.push_back(ky[i] + (ky[i + 1] - ky[i]) * (1 - std::cos(3.14159265 * u)) / 2);
            double g = base - pinchAt(t, pinches);
            if (inStraight(t)) g = std::min(g, std::max(minGapUnits, base * 0.7)); // straight fly
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

    if (s.gravity && kt.size() > 4) {
        size_t mid = kt.size() / 2;
        out.add(obj::PortalGravityUp, tl.x(kt[mid]), std::clamp(ky[mid], 40.0, 190.0));
        out.add(obj::PortalGravityDown, tl.x(s.t1) - 60, 45);
        used("gravity portal");
    }
}

// ---------------------------------------------------------------------------------------
// Ball / Spider: gravity flips on the beat inside a corridor of varying height
// ---------------------------------------------------------------------------------------

void Gen::genFlip(size_t si, bool spider) {
    auto& s = segs[si];
    double C = s.corridor;
    double x0 = tl.x(s.t0) + 75, x1 = tl.x(s.t1) - 15;
    for (double x = x0; x <= x1; x += 30) {
        out.add(obj::Block, x, C + 15);
        out.add(obj::Block, x, C + 45);
    }
    double a = s.t0 + 0.55, b = s.t1 - 0.6;
    if (b <= a) return;
    double gap = spider ? std::max(D.minGap, 0.2) : std::max(D.minGap * 1.6, 0.45);
    int sub = spider && diff() >= 3 ? 2 : 1;
    auto ev = pick(candidates(s, a, b, sub + 1), targetCount(s, a, b, spider ? 0.8 : 0.55), gap);
    if (ev.size() % 2 == 1) ev.pop_back(); // must end on the floor
    // ball needs time to cross the corridor: taller corridor -> later punishment
    double travel = spider ? 0 : std::sqrt(2 * (C - 30) / (0.6 * CUBE_G));
    double late = spider ? D.spiderLate : std::max(0.19, std::min(D.ballLate, travel + 0.05));
    double early = spider ? 0.05 : 0.0;

    // spike rows on the wrong surface; on strong beats occasionally a "triple" that
    // sticks out further (two stacked rows on the surface would be unfair, so it is a
    // denser single row instead)
    auto row = [&](double ta, double tb, bool ceiling) {
        double xa = tl.x(ta) + 18, xb = tl.x(tb) - 18;
        if (ceiling) xa = std::max(xa, x0);
        for (double x = xa; x <= xb; x += 30) {
            spike(x, ceiling ? C - 15 : 15, ceiling, chunkGroup(si, ta, x, ceiling));
        }
    };
    if (!ev.empty() && ev.front().t - a > 0.6) row(a, ev.front().t - early, true);
    bool onCeiling = false;
    for (size_t k = 0; k < ev.size(); k++) {
        double tk = ev[k].t;
        double tn = k + 1 < ev.size() ? ev[k + 1].t : s.t1 - 0.4;
        row(tk + late, tn - early, onCeiling);
        onCeiling = !onCeiling;
        clicks++;
    }
    used(spider ? "spider" : "ball");
    if (C != 120) used(C < 120 ? "low corridor" : "tall corridor");
}

} // namespace rg::gen
