#include "Common.hpp"

namespace rg::gen {

// ---------------------------------------------------------------------------------------
// Song structure -> parts with gamemode, speed and modifiers
// ---------------------------------------------------------------------------------------

void Gen::planSegments() {
    double P = beatLen();
    double dur = lt(A.duration);
    std::vector<Section> secs;
    for (auto s : A.sections) {
        s.start = std::max(0.0, lt(s.start));
        s.end = std::min(dur, lt(s.end));
        if (s.end - s.start > 0.1) secs.push_back(s);
    }
    if (secs.empty()) secs.push_back({0, dur, 0.5f, 1, false, 0});
    secs.front().start = 0;

    // split long sections into 8-bar pieces (16 for calm ones) so modes change regularly,
    // the way creators switch gamemodes on phrase boundaries
    std::vector<Seg> raw;
    for (auto& s : secs) {
        // calm parts are long; intense parts switch every 4 bars (sometimes 2) for surprise
        int bars = s.level == 0 ? 16 : s.level == 1 ? 8 : (diff() >= 2 && rnd() < 0.5 ? (rnd() < 0.3 ? 2 : 4) : 8);
        double maxLen = bars * 4 * P;
        int pieces = std::max(1, int(std::round((s.end - s.start) / maxLen)));
        double len = (s.end - s.start) / pieces;
        for (int k = 0; k < pieces; k++) {
            Seg g{s.start + k * len, s.start + (k + 1) * len, Mode::Cube, 1, s.level, k == 0 && s.drop};
            raw.push_back(g);
        }
    }
    for (auto& s : raw) {
        if (!segs.empty() && s.t1 - s.t0 < std::max(3.0, 4 * P)) {
            segs.back().t1 = s.t1;
            segs.back().level = std::max(segs.back().level, s.level);
            continue;
        }
        segs.push_back(s);
    }

    // choose modes: calm parts favour cube/ship, drops favour wave/spider/fast cube
    Mode prev = Mode::Cube, prev2 = Mode::Cube;
    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        std::vector<std::pair<Mode, double>> opts;
        auto addOpt = [&](Mode m, double w) {
            if (i > 0 && m == prev) return;
            if (i > 1 && m == prev2) w *= 0.6; // avoid A-B-A ping-pong
            opts.push_back({m, w});
        };
        switch (s.level) {
            case 0: addOpt(Mode::Cube, 2.0); addOpt(Mode::Ship, 1.2); if (diff() >= 1) addOpt(Mode::Ball, 0.5); break;
            case 1:
                addOpt(Mode::Cube, 2.0); addOpt(Mode::Ball, 1.0); addOpt(Mode::Ship, 1.0);
                if (D.spider) addOpt(Mode::Spider, 0.8);
                break;
            case 2:
                addOpt(Mode::Cube, 1.8); addOpt(Mode::Ball, 1.0); addOpt(Mode::Ship, 0.9);
                if (D.spider) addOpt(Mode::Spider, 1.0);
                if (D.wave) addOpt(Mode::Wave, 1.0);
                break;
            default:
                addOpt(Mode::Cube, 1.5); addOpt(Mode::Ship, 0.7);
                if (D.wave) addOpt(Mode::Wave, s.drop ? 2.5 : 1.5);
                if (D.spider) addOpt(Mode::Spider, 1.2);
                if (!D.wave && !D.spider) addOpt(Mode::Ball, 1.0);
                break;
        }
        if (i == 0) s.mode = Mode::Cube; // the level starts as a cube
        else {
            double total = 0;
            for (auto& o : opts) total += o.second;
            double r = rnd() * total;
            s.mode = opts.back().first;
            for (auto& o : opts) {
                if ((r -= o.second) <= 0) { s.mode = o.first; break; }
            }
        }
        // One speed for the whole level. Every speed portal is a point where the game and the
        // music can drift apart (by how many units early a portal fires); a single speed set
        // at the spawn point keeps the sync exact.
        int rank = D.speedRank;
        s.speedRank = rank;

        // modifiers
        bool flying = s.mode == Mode::Ship || s.mode == Mode::Wave;
        s.mini = flying && s.level >= 2 && diff() >= 3 && rnd() < 0.4;
        // a mini wave climbs twice as fast: it must still be able to zigzag between eighths
        if (s.mini && s.mode == Mode::Wave && SPEED_UPS[rank] * 2 * P / 2 > 150) s.mini = false;
        s.gravity = flying && s.level >= 2 && diff() >= 2 && rnd() < 0.45;
        s.mirror = i > 0 && s.drop && diff() >= 2 && rnd() < 0.3;
        s.style = rndInt(4);
        if (s.mode == Mode::Ball || s.mode == Mode::Spider) {
            static constexpr double heights[3] = {90, 120, 150};
            s.corridor = heights[rndInt(3)];
            if (s.mode == Mode::Ball && s.corridor < 120) s.corridor = 120;
        }
        prev2 = prev;
        prev = s.mode;
        modeCount[int(s.mode)]++;
    }
    // same gamemode, faster second half: a speed-up in the middle of an intense part
    if (false) { // speed changes inside the level are disabled for exact sync
        for (size_t i = 1; i < segs.size(); i++) {
            auto& s = segs[i];
            if (s.level < 2 || s.t1 - s.t0 < 15 * P || rnd() > 0.25) continue;
            int cap = (s.mode == Mode::Ship || s.mode == Mode::Wave) ? 3 : 4;
            if (s.speedRank >= cap) continue;
            double mid = (s.t0 + s.t1) / 2, split = -1;
            for (size_t k = 0; k < beats.size(); k++) {
                if (down[k] && (split < 0 || std::abs(beats[k] - mid) < std::abs(split - mid))) split = beats[k];
            }
            if (split < s.t0 + 6 * P || split > s.t1 - 6 * P) continue;
            Seg second = s;
            second.t0 = split;
            second.speedRank = s.speedRank + 1;
            second.drop = false;
            second.mirror = false;
            s.t1 = split;
            segs.insert(segs.begin() + long(i) + 1, second);
            modeCount[int(second.mode)]++;
            used("speed-up inside a part");
            i++;
        }
    }
    for (size_t i = 0; i + 1 < segs.size(); i++) {
        segs[i].build = segs[i + 1].drop && segs[i + 1].level > segs[i].level;
    }
}

void Gen::buildTimeline() {
    tl.ks.push_back({0, 0, SPEED_UPS[segs.front().speedRank]});
    for (size_t i = 1; i < segs.size(); i++) {
        if (segs[i].speedRank != segs[i - 1].speedRank) {
            tl.ks.push_back({segs[i].t0, tl.x(segs[i].t0), SPEED_UPS[segs[i].speedRank]});
        }
    }
}

// ---------------------------------------------------------------------------------------
// Rhythm candidates
// ---------------------------------------------------------------------------------------

std::vector<Cand> Gen::candidates(Seg const&, double a, double b, int sub) const {
    std::vector<Cand> c;
    double P = beatLen();
    for (size_t i = 0; i < beats.size(); i++) {
        double t = beats[i];
        if (t + P < a || t > b) continue;
        size_t mi = beatIdx[i];
        if (t >= a && t <= b) {
            float w = 0.55f * A.beatStrength[mi] + 0.25f * A.beatEnergy[mi] + (down[i] ? 0.35f : 0.f);
            c.push_back({t, w, bool(down[i])});
        }
        if (sub >= 2) {
            double h = t + P / 2;
            float s = A.onsetAt(mt(h), 0.04);
            if (h >= a && h <= b && s > 0.25f) c.push_back({h, 0.9f * s, false});
        }
        if (sub >= 4) {
            for (double q : {t + P / 4, t + 3 * P / 4}) {
                float s = A.onsetAt(mt(q), 0.03);
                if (q >= a && q <= b && s > 0.35f) c.push_back({q, 0.8f * s, false});
            }
        }
    }
    std::sort(c.begin(), c.end(), [](auto& l, auto& r) { return l.t < r.t; });
    return c;
}

std::vector<Cand> Gen::pick(std::vector<Cand> c, double count, double minGap) const {
    std::stable_sort(c.begin(), c.end(), [](auto& l, auto& r) { return l.w > r.w; });
    std::set<double> taken;
    std::vector<Cand> res;
    for (auto& e : c) {
        if (double(res.size()) >= count) break;
        auto it = taken.lower_bound(e.t - minGap + 1e-6);
        if (it != taken.end() && *it < e.t + minGap - 1e-6) continue;
        taken.insert(e.t);
        res.push_back(e);
    }
    std::sort(res.begin(), res.end(), [](auto& l, auto& r) { return l.t < r.t; });
    return res;
}

double Gen::targetCount(Seg const& s, double a, double b, double mult) const {
    return std::max(1.0, (b - a) / beatLen() * D.density * LEVEL_DENSITY[std::clamp(s.level, 0, 3)] * mult);
}

float Gen::kickAt(double t) const {
    int i = A.nearestBeat(mt(t));
    if (i < 0 || std::abs(A.beats[size_t(i)] - mt(t)) > 0.05) return 0;
    return A.beatKick[size_t(i)];
}

float Gen::snareAt(double t) const {
    int i = A.nearestBeat(mt(t));
    if (i < 0 || std::abs(A.beats[size_t(i)] - mt(t)) > 0.05) return 0;
    return A.beatSnare[size_t(i)];
}

float Gen::sustainAt(double t) const {
    auto o = A.onsetNear(mt(t), 0.05);
    return o ? o->sustain : 0.f;
}

// ---------------------------------------------------------------------------------------
// Objects shared by modules
// ---------------------------------------------------------------------------------------

int Gen::chunkGroup(size_t segIdx, double t, double x, bool upper) {
    double half = 2 * beatLen(); // half-bar chunks: staggered entrances
    long idx = long(std::floor((t - segs[segIdx].t0) / half));
    long k = (long(segIdx) << 21) | (idx << 1) | (upper ? 1 : 0);
    auto it = chunks.find(k);
    if (it == chunks.end()) {
        Chunk c;
        c.group = nextGroup++;
        c.dir = upper ? 1 : -1;
        it = chunks.emplace(k, c).first;
    }
    it->second.xMin = std::min(it->second.xMin, x);
    it->second.xMax = std::max(it->second.xMax, x);
    return it->second.group;
}

void Gen::spike(double x, double y, bool pointDown, int group) {
    auto& o = out.add(obj::Spike, x, y);
    if (pointDown) o.kv(key::Rotation, 180);
    o.group(group);
}

void Gen::portal(size_t i) {
    auto& s = segs[i];
    double x = tl.x(s.t0) + 30;
    // The timeline switches speed exactly at x(t0), the same way GD 2.2 converts music time
    // to position (the change happens at the portal's x). The portal sits exactly there; a
    // column of them so the player meets one at any height.
    if (i > 0 && s.speedRank != segs[i - 1].speedRank) {
        for (double y : {15.0, 75.0, 135.0, 195.0}) out.add(SPEED_PORTAL[s.speedRank], tl.x(s.t0), y);
    }
    // the level speed, set right where the player spawns (overlapping on the first frame),
    // whatever start speed the level had saved before
    if (i == 0) for (double y : {15.0, 75.0, 135.0}) out.add(SPEED_PORTAL[s.speedRank], 0, y);
    // Safety net: the same speed again every 4 bars. A portal of the current speed does
    // nothing, but if the spawn portal / start speed were not applied, the first of these
    // fixes the speed early, leaving a small constant offset instead of a growing drift.
    if (i == 0) {
        double P = beatLen();
        for (double t = 0.15; t < segs.back().t1; t += t < 1 ? 16 * P - 0.15 : 16 * P) {
            for (double y : {15.0, 75.0, 135.0, 195.0}) out.add(SPEED_PORTAL[s.speedRank], tl.x(t), y);
        }
        used("speed safety portals");
    }
    if (s.mirror) { out.add(obj::PortalMirrorOn, x - 30, 45); used("mirror portal"); }
    if (s.mini) { out.add(obj::PortalSizeMini, x + 45, 45); used("mini portal"); }
    if (i == 0) return;
    int id = obj::PortalCube;
    switch (s.mode) {
        case Mode::Cube: id = obj::PortalCube; break;
        case Mode::Ship: id = obj::PortalShip; break;
        case Mode::Ball: id = obj::PortalBall; break;
        case Mode::Wave: id = obj::PortalWave; break;
        case Mode::Spider: id = obj::PortalSpider; break;
    }
    out.add(id, x + 15, 45);
}

void Gen::portalEnd(size_t i) {
    auto& s = segs[i];
    double x = tl.x(s.t1) - 30;
    if (s.mini) out.add(obj::PortalSizeNormal, x, 45);
    if (s.mirror) out.add(obj::PortalMirrorOff, x + 15, 45);
}

// ---------------------------------------------------------------------------------------

GenResult Gen::run() {
    GenResult res;
    if (A.beats.empty() || A.duration < 5) {
        res.summary = "Song is too short or no beats were detected.";
        return res;
    }
    for (size_t i = 0; i < A.beats.size(); i++) {
        double t = lt(A.beats[i]);
        if (t < 0.3) continue;
        beats.push_back(t);
        down.push_back(A.downbeat[i]);
        beatIdx.push_back(i);
    }

    planSegments();
    buildTimeline();

    for (size_t i = 0; i < segs.size(); i++) {
        portal(i);
        switch (segs[i].mode) {
            case Mode::Cube: genCube(i); break;
            case Mode::Ship: genShip(i); break;
            case Mode::Ball: genFlip(i, false); break;
            case Mode::Spider: genFlip(i, true); break;
            case Mode::Wave: genWave(i); break;
        }
        portalEnd(i);
    }
    if (O.effects) effects();

    res.objects = out.finish();
    res.objectCount = out.count;
    res.startSpeed = SPEED_ENUM[segs.front().speedRank];

    std::string feat;
    for (auto& [name, n] : features) {
        if (!feat.empty()) feat += ", ";
        feat += name + " x" + std::to_string(n);
    }
    char buf[640];
    std::snprintf(
        buf, sizeof(buf),
        "%s | %s | %zu parts (cube %d, ship %d, ball %d, wave %d, spider %d) | ~%d clicks | %d objects | "
        "cube pattern space ~1e%.0f per bar",
        A.summary().c_str(), difficultyName(O.difficulty), segs.size(), modeCount[0], modeCount[1],
        modeCount[2], modeCount[3], modeCount[4], clicks, out.count, patternSpace
    );
    res.summary = buf;
    res.features = feat;
    return res;
}

} // namespace rg::gen
