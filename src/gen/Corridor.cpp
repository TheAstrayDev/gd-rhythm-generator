#include "Common.hpp"

// Ship, wave, ufo, swing and the dual wave are "path-first": the intended player path is built
// from the music, then the corridor is carved around it. Musical accents pinch the corridor so
// the player feels the hit, long notes become straight tight flights.

namespace rg::gen {

// ---------------------------------------------------------------------------------------
// Corridor walls around one or more lanes.
//
// Every 30-unit column finds the free interval of each lane (its path +- half the free
// height, over the column and a hitbox width around it), merges overlapping lanes and fills
// the rest with the part's tileset. Each face of a wall is fitted with the shape that hugs the
// path best without ever entering the free space: a flat tile, or a slope rising / falling
// 30 units over the column - so a wave flying at 45 degrees gets straight slope walls exactly
// parallel to it, the way creators build wave corridors. Two lanes (dual) get a divider
// between them wherever their corridors separate. Glow runs along every face (rotated along
// slopes), the inside is a plain stretched fill.
// ---------------------------------------------------------------------------------------

namespace {
constexpr double TOP = 360; // walls are drawn up to here (the flying bounds end at 300)
constexpr double DIAG = 10.6066; // 15 / sqrt(2)
enum Shape { Flat = 0, Rise = 1, Fall = -1 };
struct Face { double h; Shape shape; }; // h = lowest point of the face in the column
double faceMax(Face f) { return f.shape == Flat ? f.h : f.h + 30; }
} // namespace

void Gen::corridor(size_t si, std::vector<Lane> const& lanes, double xStart, double xEnd) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double sx0 = tl.x(s.t0);
    TileSet const& ts = TILESETS[s.tileset];
    bool slopes = rnd() < 0.25 + 0.75 * s.g.slopes;
    WallGroups wg{nextGroup++, nextGroup++, si};
    int midGroup = nextGroup++;
    int glowId = s.g.glow > 0.55 ? GLOW_LARGE : GLOW_MEDIUM;
    int slopeCount = 0;

    // a lane that ends while others go on (teleports, dual parts) keeps its last y for the
    // player's half width past its end, so no wall closes in on the player's box there
    auto sample = [&](Lane const& L, double t, double& y, double& gap) {
        double ext = (L.half + 4) / v;
        if (L.ts.empty() || t < L.ts.front() - ext || t > L.ts.back() + ext) return false;
        t = std::clamp(t, L.ts.front(), L.ts.back());
        size_t i = size_t(std::upper_bound(L.ts.begin(), L.ts.end(), t) - L.ts.begin());
        if (i == 0) i = 1;
        if (i >= L.ts.size()) i = L.ts.size() - 1;
        double f = L.ts[i] > L.ts[i - 1] ? std::clamp((t - L.ts[i - 1]) / (L.ts[i] - L.ts[i - 1]), 0.0, 1.0) : 1.0;
        y = L.ys[i - 1] + (L.ys[i] - L.ys[i - 1]) * f;
        gap = L.gaps[i - 1] + (L.gaps[i] - L.gaps[i - 1]) * f;
        return true;
    };

    // ---- emit helpers ------------------------------------------------------------------
    auto tile = [&](int id, double x, double y0, double y1, bool flipY, int group) {
        double h = y1 - y0;
        if (h < 1) return;
        auto& o = out.add(id, x, (y0 + y1) / 2);
        if (std::abs(h - 30) > 0.01) o.kv(key::ScaleY, h / 30);
        if (flipY) o.kv(key::FlipY, 1);
        o.group(group);
    };
    // a slope whose solid side is below (floor) or above (ceiling) the line; rot-0 slope = "/"
    // with the solid triangle at the lower right
    auto slope = [&](double xl, Face f, bool ceiling, int group) {
        auto& o = out.add(ts.slope, xl + 15, f.h + 15);
        bool fx = f.shape == Fall, fy = ceiling;
        if (ceiling) fx = f.shape == Rise; // "/" solid above = rot 180 = flip x + flip y
        if (fx) o.kv(key::FlipX, 1);
        if (fy) o.kv(key::FlipY, 1);
        o.group(group);
        slopeCount++;
    };
    // glow along a face, in the free space next to it
    auto glow = [&](double xl, Face f, bool ceiling, int group) {
        if (!glowOn) return;
        double xc = xl + 15;
        if (f.shape == Flat) {
            double gy = ceiling ? f.h - 15 : f.h + 15;
            if (gy < 14) return;
            auto& o = out.add(glowId, xc, gy);
            if (ceiling) o.kv(key::FlipY, 1);
            o.kv(key::NoTouch, 1).groups(group, glowGroup);
            return;
        }
        // along the slope: rotated 45 degrees, stretched to the slope's length
        double mid = f.h + 15;
        double nx = (f.shape == Rise) != ceiling ? -DIAG : DIAG;
        double ny = ceiling ? -DIAG : DIAG;
        if (mid + ny < 14) return;
        auto& o = out.add(glowId, xc + nx, mid + ny);
        o.kv(key::Rotation, f.shape == Rise ? -45 : 45).kv(key::ScaleX, 1.4142);
        if (ceiling) o.kv(key::FlipY, 1);
        o.kv(key::NoTouch, 1).groups(group, glowGroup);
    };

    // saws half-buried in the walls: the centre goes where the whole circle stays outside the
    // free space, and the wall under it is cut down to the centre
    struct SawAt { double x, y, r; bool top; int id; size_t lane; };
    std::vector<SawAt> sawsAt;
    for (size_t li = 0; li < lanes.size(); li++) {
        for (auto& bt : lanes[li].bites) {
            double xb = tl.x(bt.t), c = bt.top ? -1e18 : 1e18;
            if (xb < xStart + 30 || xb > xEnd - 30) continue;
            for (double px = xb - bt.r - 6; px <= xb + bt.r + 6; px += 2) {
                double t = s.t0 + (px - sx0) / v, y, gap;
                if (!sample(lanes[li], t, y, gap)) continue;
                c = bt.top ? std::max(c, y + gap / 2 + bt.r) : std::min(c, y - gap / 2 - bt.r);
            }
            if (std::abs(c) > 1e17 || (!bt.top && c < 12) || (bt.top && c > FLY_BOUND - 12)) continue;
            // the saw belongs to one lane's wall: it must stay out of every other lane
            bool clash = false;
            for (size_t lj = 0; lj < lanes.size() && !clash; lj++) {
                if (lj == li) continue;
                for (double px = xb - bt.r - 20; px <= xb + bt.r + 20 && !clash; px += 2) {
                    double t = s.t0 + (px - sx0) / v, y, gap;
                    if (sample(lanes[lj], t, y, gap) && y - gap / 2 < c + bt.r + 4 && y + gap / 2 > c - bt.r - 4) clash = true;
                }
            }
            if (clash) continue;
            sawsAt.push_back({xb, c, bt.r, bt.top, bt.id, li});
        }
    }

    for (double xc = xStart; xc <= xEnd; xc += 30) {
        double xl = xc - 15;
        // free interval of every active lane, with the bounds of its faces per sample
        struct Iv { double lo, hi; std::vector<std::pair<double, double>> low, high; }; // (x, bound)
        std::vector<Iv> ivs;
        for (size_t li = 0; li < lanes.size(); li++) {
            auto& L = lanes[li];
            Iv iv{1e18, -1e18, {}, {}};
            for (double px = xl - 6; px <= xl + 36 + 1e-9; px += 2) {
                double t = s.t0 + (px - sx0) / v, y, gap;
                if (!sample(L, t, y, gap)) continue;
                double lo = y - gap / 2, hi = y + gap / 2;
                for (auto& sw : sawsAt) {
                    if (sw.lane != li || std::abs(px - sw.x) > sw.r) continue;
                    if (sw.top) hi = std::max(hi, sw.y);
                    else lo = std::min(lo, sw.y);
                }
                iv.low.push_back({px, lo});
                iv.high.push_back({px, hi});
                iv.lo = std::min(iv.lo, lo);
                iv.hi = std::max(iv.hi, hi);
            }
            if (!iv.low.empty()) ivs.push_back(std::move(iv));
        }
        if (ivs.empty()) continue;
        std::sort(ivs.begin(), ivs.end(), [](auto& a, auto& b) { return a.lo < b.lo; });
        std::vector<Iv> merged;
        for (auto& iv : ivs) {
            if (!merged.empty() && iv.lo <= merged.back().hi + 1) {
                auto& m = merged.back();
                m.hi = std::max(m.hi, iv.hi);
                m.low.insert(m.low.end(), iv.low.begin(), iv.low.end());
                m.high.insert(m.high.end(), iv.high.begin(), iv.high.end());
            } else merged.push_back(std::move(iv));
        }
        // fit the faces: floor faces as high as possible, ceiling faces as low as possible
        auto fitFloor = [&](Iv const& iv) {
            double flat = 1e18, rise = 1e18, fall = 1e18;
            for (auto& [px, b] : iv.low) {
                double u = std::clamp(px - xl, 0.0, 30.0);
                flat = std::min(flat, b);
                rise = std::min(rise, b - u);
                fall = std::min(fall, b - (30 - u));
            }
            Face f{flat, Flat};
            if (slopes && rise + 15 > f.h + 4 && rise >= fall) f = {rise, Rise};
            else if (slopes && fall + 15 > f.h + 4) f = {fall, Fall};
            return f;
        };
        auto fitCeil = [&](Iv const& iv) {
            double flat = -1e18, rise = -1e18, fall = -1e18;
            for (auto& [px, b] : iv.high) {
                double u = std::clamp(px - xl, 0.0, 30.0);
                flat = std::max(flat, b);
                rise = std::max(rise, b - u);
                fall = std::max(fall, b - (30 - u));
            }
            Face f{flat, Flat};
            if (slopes && rise + 15 < f.h - 4 && rise <= fall) f = {rise, Rise};
            else if (slopes && fall + 15 < f.h - 4) f = {fall, Fall};
            return f;
        };

        // bottom wall (on the ground)
        {
            Face f = fitFloor(merged.front());
            int g = wg.bottom;
            if (f.shape != Flat && f.h + 30 > 5) {
                slope(xl, f, false, g);
                if (f.h >= 3) tile(FILL_BLOCK, xc, 0, f.h, false, g);
                glow(xl, f, false, g);
                wg.bottomEnd = xc;
            } else if (f.shape == Flat && f.h >= 22.5) {
                if (f.h >= 33) tile(ts.top, xc, f.h - 30, f.h, false, g), tile(FILL_BLOCK, xc, 0, f.h - 30, false, g);
                else tile(ts.top, xc, 0, f.h, false, g);
                glow(xl, f, false, g);
                wg.bottomEnd = xc;
            }
        }
        // top wall
        {
            Face f = fitCeil(merged.back());
            int g = wg.top;
            if (f.shape != Flat && f.h < TOP - 30) {
                slope(xl, f, true, g);
                tile(FILL_BLOCK, xc, f.h + 30, TOP, false, g);
                glow(xl, f, true, g);
                wg.topEnd = xc;
            } else if (f.shape == Flat && f.h < TOP - 30) {
                tile(ts.top, xc, f.h, f.h + 30, true, g);
                tile(FILL_BLOCK, xc, f.h + 30, TOP, false, g);
                glow(xl, f, true, g);
                wg.topEnd = xc;
            }
        }
        // dividers between separated lanes (dual)
        for (size_t k = 0; k + 1 < merged.size(); k++) {
            Face b = fitCeil(merged[k]), t = fitFloor(merged[k + 1]);
            double bTop = faceMax(b);
            if (bTop > t.h) b = {bTop, Flat}, t = {t.h, Flat}; // no room for both slopes
            if (t.shape == Flat && b.shape == Flat && t.h - b.h < 6) continue;
            if (b.shape != Flat) slope(xl, b, true, midGroup);
            if (t.shape != Flat) slope(xl, t, false, midGroup);
            double lo = b.shape != Flat ? b.h + 30 : b.h, hi = t.h;
            bool thin = hi - lo < 30 && (b.shape == Flat || t.shape == Flat); // one squashed face tile
            if (b.shape == Flat && hi - lo >= 30) tile(ts.top, xc, lo, lo + 30, true, midGroup), lo += 30;
            if (t.shape == Flat && hi - lo >= 30) tile(ts.top, xc, hi - 30, hi, false, midGroup), hi -= 30;
            tile(thin ? ts.top : FILL_BLOCK, xc, lo, hi, false, midGroup);
            glow(xl, b, true, midGroup);
            glow(xl, t, false, midGroup);
        }
    }
    for (auto& sw : sawsAt) saw(sw.id, sw.x, sw.y, sw.top ? wg.top : wg.bottom);
    for (auto& L : lanes) {
        if (L.ts.empty()) continue;
        for (double t = L.ts.front(); t <= L.ts.back(); t += 1.0 / 120) {
            double x = sx0 + (t - s.t0) * v, y, gap;
            if (x < xStart || x > xEnd || !sample(L, t, y, gap)) continue;
            trace.push_back({x, y, L.half});
        }
    }
    if (!sawsAt.empty()) features["saws in the walls"] += int(sawsAt.size());
    walls.push_back(wg);
    if (slopeCount) features["corridor slopes"] += slopeCount;
}

// a round hazard; recorded so glow and other decoration keep away from it
void Gen::saw(int id, double x, double y, int group) {
    out.add(id, x, y).group(group);
    hazards[std::lround(x / 30)].push_back({x, y});
}

} // namespace rg::gen
