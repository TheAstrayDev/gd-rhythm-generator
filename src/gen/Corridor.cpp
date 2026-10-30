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
            wg.topEnd = xc;
        }
        double topOfBottom = std::floor((lo - gap / 2) / 7.5) * 7.5;
        if (topOfBottom >= 22.5) {
            out.add(obj::Block, xc, topOfBottom / 2).kv(key::ScaleY, topOfBottom / 30).group(wg.bottom);
            wg.bottomEnd = xc;
        }
        wallFaces.push_back({xc, topOfBottom >= 22.5 ? topOfBottom : 0, bottomOfTop < 330 ? bottomOfTop : 360, si,
                             wg.top, wg.bottom});
    }
    walls.push_back(wg);
}

} // namespace rg::gen
