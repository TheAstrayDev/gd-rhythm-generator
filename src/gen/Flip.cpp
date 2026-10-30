#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Ball / Spider: gravity flips on the music inside a living corridor.
//
//   flips    : clicks on onsets; sometimes a gravity portal does the flip instead. A portal
//              only goes on a weaker hit with at least two beats of calm before and after,
//              and both surfaces are kept free of hazards for a beat around it, so an
//              instinctive click there can never throw the player into spikes
//   terrain  : while the player is on one surface, the surface it just left shifts up or
//              down by a block (never a wall in front of the player)
//   hazards  : per flip one of several styles on the wrong surface - full spike row,
//              spike clusters, block bumps, a short row just where the punishment matters
//   search   : pool of plans + annealing, scored for variety and sync
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
enum HazardStyle { HRow, HClusters, HBlocks, HShort, HAZARD_COUNT };
struct Step { double t; double h; }; // surface height from time t on
struct FlipPlan { std::vector<int> portal, shift, style; };
struct FlipLayout {
    std::vector<Step> floor, ceil;
    struct Hazard { double t0, t1; bool ceiling; HazardStyle style; };
    std::vector<Hazard> hazards;
    struct Portal { double t; bool toCeil; double y; };
    std::vector<Portal> portals;
    int clicks = 0, changes = 0;
    double score = -1e18;
};
double heightAt(std::vector<Step> const& st, double t) {
    double h = st.front().h;
    for (auto& s : st) if (s.t <= t) h = s.h;
    return h;
}
} // namespace

void Gen::genFlip(size_t si, bool spider) {
    auto& s = segs[si];
    double P = beatLen();
    double x0 = tl.x(s.t0) + 75, x1 = tl.x(s.t1) - 15;
    double a = s.t0 + 0.55, b = s.t1 - 0.6;
    if (b <= a) return;
    double gap = spider ? std::max(D.minGap, 0.2) : std::max(D.minGap * 1.6, 0.45);
    int sub = spider && diff() >= 3 ? 2 : 1;
    auto ev = pick(candidates(s, a, b, sub + 1), targetCount(s, a, b, spider ? 0.8 : 0.55), gap);
    if (ev.size() % 2 == 1) ev.pop_back(); // must end on the floor
    double minGapUnits = spider ? 60 : 90;
    double ballG = 0.6 * CUBE_G;
    bool shifting = diff() >= 1;
    bool portalsAllowed = spider ? diff() >= 2 : diff() >= 3;

    // a flip may become a portal only with calm around it and on a weaker hit
    std::vector<char> portalOk(ev.size(), 0);
    for (size_t k = 1; k + 1 < ev.size(); k++) {
        bool calm = ev[k].t - ev[k - 1].t >= 2 * P && ev[k + 1].t - ev[k].t >= 2 * P;
        portalOk[k] = char(portalsAllowed && calm && ev[k].w < 0.85f);
    }

    auto build = [&](FlipPlan const& pl) {
        FlipLayout L;
        double F = 0, C = s.corridor;
        L.floor.push_back({s.t0, F});
        L.ceil.push_back({s.t0, C});
        bool onCeil = false;
        double sync = 0;
        int portalCount = 0;
        std::set<int> gapsSeen, stylesSeen;
        if (!ev.empty() && ev.front().t - a > 0.6) L.hazards.push_back({a, ev.front().t - 0.05, true, HRow});
        for (size_t k = 0; k < ev.size(); k++) {
            double tk = ev[k].t;
            double tn = k + 1 < ev.size() ? ev[k + 1].t : s.t1 - 0.4;
            double tr = spider ? 0 : std::sqrt(2 * std::max(0.0, C - F - 30) / ballG);
            double late = spider ? D.spiderLate : std::max(0.19, std::min(D.ballLate, tr + 0.05));
            double early = spider ? 0.05 : 0.0;
            bool isPortal = pl.portal[k] && portalOk[k];
            bool nextPortal = k + 1 < ev.size() && pl.portal[k + 1] && portalOk[k + 1];
            if (isPortal) {
                L.portals.push_back({tk, !onCeil, onCeil ? C - 45 : F + 45});
                portalCount++;
                late = std::max(late, P); // clean zone after the portal
            } else {
                L.clicks++;
                sync += ev[k].w;
            }
            if (nextPortal) early = std::max(early, P); // clean zone before the next portal
            // the surface just left may shift once the player has arrived on the other one
            int d = shifting ? pl.shift[k] : 0;
            double tShift = tk + tr + 0.1;
            if (d != 0 && tShift < tn - 0.15) {
                if (!onCeil) {
                    double nf = std::clamp(F + d * 30, 0.0, 90.0);
                    if (C - nf >= minGapUnits && nf != F) F = nf, L.floor.push_back({tShift, F}), L.changes++;
                } else {
                    double nc = std::clamp(C + d * 30, F + minGapUnits, 240.0);
                    if (nc != C) C = nc, L.ceil.push_back({tShift, C}), L.changes++;
                }
            }
            gapsSeen.insert(int(C - F));
            HazardStyle st = HazardStyle(pl.style[k]);
            if (st == HBlocks && C - F < minGapUnits + 30) st = HRow; // bumps need the room
            stylesSeen.insert(int(st));
            if (tn - early > tk + late) L.hazards.push_back({tk + late, tn - early, onCeil, st});
            onCeil = !onCeil;
        }
        double portalShare = ev.empty() ? 0 : double(portalCount) / ev.size();
        L.score = 3.0 * L.changes + 2.5 * portalCount + 1.5 * sync + 2.0 * gapsSeen.size() + 2.0 * stylesSeen.size()
                  - (portalShare > 0.25 ? 15.0 * (portalShare - 0.25) * ev.size() : 0.0);
        return L;
    };

    // ---- search: pool, then annealing on the best plan --------------------------------
    auto randomPlan = [&]() {
        FlipPlan pl;
        for (size_t k = 0; k < ev.size(); k++) {
            pl.portal.push_back(rnd() < 0.25 ? 1 : 0);
            pl.shift.push_back(rndInt(3) - 1);
            pl.style.push_back(rndInt(HAZARD_COUNT));
        }
        return pl;
    };
    FlipPlan bestPlan = randomPlan();
    FlipLayout best = build(bestPlan);
    for (int i = 0; i < 24; i++) {
        auto pl = randomPlan();
        auto L = build(pl);
        if (L.score > best.score) best = std::move(L), bestPlan = pl;
    }
    FlipPlan cur = bestPlan;
    double curScore = best.score;
    int improved = 0;
    for (int r = 0; r < 150 && !ev.empty(); r++) {
        double temp = 2.0 * (1.0 - r / 150.0);
        FlipPlan pl = cur;
        size_t k = size_t(rndInt(int(ev.size())));
        double m = rnd();
        if (m < 0.3) pl.portal[k] ^= 1;
        else if (m < 0.65) pl.shift[k] = rndInt(3) - 1;
        else pl.style[k] = rndInt(HAZARD_COUNT);
        auto L = build(pl);
        if (L.score > curScore || rnd() < std::exp((L.score - curScore) / std::max(0.05, temp))) cur = pl, curScore = L.score;
        if (L.score > best.score) best = std::move(L), bestPlan = pl, improved++;
    }
    features[spider ? "spider improvement steps" : "ball improvement steps"] += improved;

    // ---- emit ----------------------------------------------------------------------
    double v = SPEED_UPS[s.speedRank];
    double sx0 = tl.x(s.t0);
    auto tAt = [&](double x) { return s.t0 + (x - sx0) / v; };
    for (double x = x0; x <= x1; x += 30) {
        double t = tAt(x);
        double F = heightAt(best.floor, t), C = heightAt(best.ceil, t);
        if (F > 0) {
            int g = chunkGroup(si, t, x, false);
            for (double y = 15; y < F; y += 30) solidBlock(x, y, g, si);
        }
        solidBlock(x, C + 15, 0, si);
        solidBlock(x, C + 45, 0, si);
    }
    auto changeNear = [&](std::vector<Step> const& st, double x) {
        for (size_t i = 1; i < st.size(); i++) if (std::abs(tl.x(st[i].t) - x) < 35) return true;
        return false;
    };
    static char const* styleNames[] = {"spike row", "spike clusters", "block bumps", "short spike row"};
    for (auto& hz : best.hazards) {
        double xa = tl.x(hz.t0) + 18, xb = tl.x(hz.t1) - 18;
        if (hz.ceiling) xa = std::max(xa, x0);
        if (hz.style == HShort) xb = std::min(xb, xa + 90); // only where not flipping kills
        int n = 0;
        for (double x = xa; x <= xb; x += 30, n++) {
            if (hz.style == HClusters && n % 4 >= 2) continue;
            double t = tAt(x);
            auto& steps = hz.ceiling ? best.ceil : best.floor;
            if (changeNear(steps, x)) continue;
            double surf = heightAt(steps, t);
            int g = chunkGroup(si, hz.t0, x, hz.ceiling);
            if (hz.style == HBlocks) {
                solidBlock(x, hz.ceiling ? surf - 15 : surf + 15, g, si);
            } else {
                spike(x, hz.ceiling ? surf - 15 : surf + 15, hz.ceiling, g);
            }
        }
        used(styleNames[hz.style]);
    }
    for (auto& p : best.portals) {
        out.add(p.toCeil ? obj::PortalGravityUp : obj::PortalGravityDown, tl.x(p.t) + 15, p.y);
    }
    clicks += best.clicks;
    used(spider ? "spider" : "ball");
    if (best.changes) features[spider ? "spider shifting terrain" : "ball shifting terrain"] += best.changes;
    if (!best.portals.empty()) features["gravity portal flip"] += int(best.portals.size());
    if (s.corridor != 120) used(s.corridor < 120 ? "low corridor" : "tall corridor");
}

} // namespace rg::gen
