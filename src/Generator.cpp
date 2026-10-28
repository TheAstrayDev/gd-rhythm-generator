#include "Generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>

namespace rg {

char const* difficultyName(Difficulty d) {
    static char const* names[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Demon"};
    return names[int(d)];
}

char const* modeName(Mode m) {
    static char const* names[] = {"Cube", "Ship", "Ball", "Wave", "Spider"};
    return names[int(m)];
}

namespace {

// ---------------------------------------------------------------------------------------
// GD constants
// ---------------------------------------------------------------------------------------

// Speeds ordered by rank (0 = 0.5x .. 4 = 4x), in units per second (1 block = 30 units).
constexpr double SPEED_UPS[5] = {251.16, 311.58, 387.42, 468.0, 576.0};
constexpr int SPEED_PORTAL[5] = {200, 201, 202, 203, 1334};
constexpr int SPEED_ENUM[5] = {1, 0, 2, 3, 4}; // GD's Speed enum for each rank

namespace obj {
constexpr int Block = 1;
constexpr int Spike = 8;
constexpr int PadYellow = 35;
constexpr int OrbYellow = 36;
constexpr int OrbPink = 141;
constexpr int PortalCube = 12;
constexpr int PortalShip = 13;
constexpr int PortalBall = 47;
constexpr int PortalWave = 660;
constexpr int PortalSpider = 1331;
constexpr int TrigColor = 899;
constexpr int TrigMove = 901;
constexpr int TrigPulse = 1006;
constexpr int TrigAlpha = 1007;
constexpr int TrigShake = 1520;
constexpr int TrigCamRotate = 2015;
} // namespace obj

namespace key {
constexpr int FlipY = 5, Rotation = 6, Red = 7, Green = 8, Blue = 9, Duration = 10;
constexpr int TargetColor = 23, MoveX = 28, MoveY = 29, Easing = 30, Opacity = 35;
constexpr int FadeIn = 45, Hold = 46, FadeOut = 47, PulseMode = 48, TargetGroup = 51, PulseTargetType = 52;
constexpr int Groups = 57, Degrees = 68, ShakeStrength = 75, ShakeInterval = 84, EasingRate = 85;
constexpr int ScaleX = 128, ScaleY = 129;
} // namespace key

namespace ease {
constexpr int None = 0, InOut = 1, Out = 3, ElasticOut = 6, BounceOut = 9, ExpOut = 12, SineInOut = 13, BackOut = 18;
}

// Approximate GD player physics (units, seconds). Vertical physics does not depend on speed.
constexpr double CUBE_G = 3449.0;   // gravity
constexpr double CUBE_V = 670.8;    // jump velocity  -> ~65 units high, ~0.39 s airtime
constexpr double ORB_YELLOW = 1.0;  // multipliers of CUBE_V
constexpr double ORB_PINK = 0.72;
constexpr double PAD_YELLOW = 1.36;
constexpr double SPIKE_TOP = 22.0;  // top of a ground spike's hitbox
constexpr double SPIKE_HALF = 3.0;  // half width of a spike's hitbox
constexpr double PLAYER_HALF = 15.0;
constexpr double CORRIDOR_C = 120.0; // ball / spider ceiling height (4 blocks)

struct DiffCfg {
    int speedRank, peakSpeedRank;
    double density;    // target clicks per beat (scaled by section energy)
    double minGap;     // min seconds between clicks
    int maxSpikes;     // per normal jump
    double margin;     // extra vertical clearance (units)
    double waveGap;    // total free space around the wave path
    double shipGap;    // total free space around the ship path
    double shipSpacing;// seconds between ship path keypoints
    double ballLate;   // allowed lateness for ball flips (s)
    double spiderLate; // allowed lateness for spider flips (s)
    bool orbs, pads, spider, wave;
};

constexpr DiffCfg DIFFS[DIFFICULTY_COUNT] = {
    /* Easy   */ {1, 1, 0.35, 0.55, 1, 14, 110, 165, 1.10, 0.30, 0.16, false, true, false, false},
    /* Normal */ {1, 2, 0.55, 0.40, 2, 11, 92, 140, 0.90, 0.26, 0.13, true, true, true, false},
    /* Hard   */ {2, 2, 0.80, 0.28, 2, 8, 78, 120, 0.75, 0.22, 0.10, true, true, true, true},
    /* Harder */ {2, 3, 1.00, 0.22, 3, 6, 68, 108, 0.62, 0.20, 0.08, true, true, true, true},
    /* Insane */ {3, 3, 1.30, 0.17, 3, 4, 58, 96, 0.52, 0.18, 0.07, true, true, true, true},
    /* Demon  */ {3, 4, 1.60, 0.13, 3, 3, 50, 86, 0.45, 0.17, 0.06, true, true, true, true},
};

constexpr double LEVEL_DENSITY[4] = {0.55, 0.8, 1.0, 1.25};

// ---------------------------------------------------------------------------------------
// Object string writer
// ---------------------------------------------------------------------------------------

std::string num(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.2f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

class Out {
public:
    std::string str;
    int count = 0;

    Out& add(int id, double x, double y) {
        if (m_open) str += ';';
        str += "1," + std::to_string(id) + ",2," + num(x) + ",3," + num(y);
        m_open = true;
        count++;
        return *this;
    }
    Out& kv(int k, double v) {
        str += ',' + std::to_string(k) + ',' + num(v);
        return *this;
    }
    Out& group(int g) {
        if (g > 0) str += ",57," + std::to_string(g);
        return *this;
    }
    std::string finish() { return m_open ? str + ';' : str; }

private:
    bool m_open = false;
};

// ---------------------------------------------------------------------------------------
// Time -> x mapping with speed changes
// ---------------------------------------------------------------------------------------

struct Timeline {
    struct K { double t, x, v; };
    std::vector<K> ks;

    K const& at(double t) const {
        size_t i = 0;
        while (i + 1 < ks.size() && ks[i + 1].t <= t) i++;
        return ks[i];
    }
    double x(double t) const {
        auto& k = at(t);
        return k.x + (t - k.t) * k.v;
    }
};

struct Seg {
    double t0, t1;
    Mode mode;
    int speedRank;
    int level;
    bool drop;
};

struct Cand {
    double t;
    float w;
    bool down;
};

struct Chunk {
    int group;
    double xMin = 1e18, xMax = -1e18;
    int dir; // +1: comes from above, -1: comes from below
};

class Gen {
public:
    Gen(Analysis const& a, GenOptions const& o) : A(a), O(o), D(DIFFS[int(o.difficulty)]), rng(o.seed) {}

    GenResult run();

private:
    Analysis const& A;
    GenOptions const& O;
    DiffCfg const& D;
    std::mt19937 rng;
    Out out;
    Timeline tl;
    std::vector<Seg> segs;
    std::vector<double> beats; // level time
    std::vector<bool> down;
    std::vector<size_t> beatIdx; // index into the analysis arrays for each level beat
    int nextGroup = 1;
    std::map<long, Chunk> chunks;
    struct WallGroups { int top, bottom; size_t seg; };
    std::vector<WallGroups> walls;
    int modeCount[5] = {0, 0, 0, 0, 0};
    int clicks = 0;

    double lt(double musicT) const { return musicT + O.syncOffset - O.songOffset; }
    double mt(double levelT) const { return levelT - O.syncOffset + O.songOffset; }
    double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    double beatLen() const { return A.beatPeriod; }

    void planSegments();
    void buildTimeline();
    std::vector<Cand> candidates(Seg const& s, double a, double b, int sub) const;
    std::vector<Cand> pick(std::vector<Cand> c, double count, double minGap) const;
    double targetCount(Seg const& s, double a, double b, double mult = 1) const;

    int chunkGroup(size_t segIdx, double t, double x, bool upper);
    void spike(double x, bool ceiling, int group);

    void genCube(size_t i);
    void genFlip(size_t i, bool spider);
    void genWave(size_t i);
    void genShip(size_t i);
    void walls2(size_t i, std::vector<double> const& ts, std::vector<double> const& ys, double gap, double xStart, double xEnd);
    void portal(size_t i);
    void effects();
};

// ---------------------------------------------------------------------------------------
// Planning
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

    // split long sections into 8-bar pieces (16 for calm ones) so modes change regularly
    std::vector<Seg> raw;
    for (auto& s : secs) {
        double maxLen = (s.level == 0 ? 16 : 8) * 4 * P;
        int pieces = std::max(1, int(std::round((s.end - s.start) / maxLen)));
        double len = (s.end - s.start) / pieces;
        for (int k = 0; k < pieces; k++) {
            raw.push_back({s.start + k * len, s.start + (k + 1) * len, Mode::Cube, 1, s.level, k == 0 && s.drop});
        }
    }
    // merge tiny segments
    for (auto& s : raw) {
        if (!segs.empty() && s.t1 - s.t0 < std::max(3.0, 4 * P)) {
            segs.back().t1 = s.t1;
            segs.back().level = std::max(segs.back().level, s.level);
            continue;
        }
        segs.push_back(s);
    }

    // choose modes
    Mode prev = Mode::Cube;
    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        std::vector<std::pair<Mode, double>> opts;
        auto addOpt = [&](Mode m, double w) { if (m != prev || i == 0) opts.push_back({m, w}); };
        switch (s.level) {
            case 0: addOpt(Mode::Cube, 2.0); addOpt(Mode::Ship, 1.2); break;
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
        int rank = s.level >= 3 ? D.peakSpeedRank : D.speedRank;
        if (s.level == 0 && rank > 1 && s.mode == Mode::Cube) rank--;
        if (s.mode == Mode::Ship || s.mode == Mode::Wave) rank = std::min(rank, 3);
        s.speedRank = rank;
        prev = s.mode;
        modeCount[int(s.mode)]++;
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

int Gen::chunkGroup(size_t segIdx, double t, double x, bool upper) {
    double bar = 4 * beatLen();
    long idx = long(std::floor((t - segs[segIdx].t0) / bar));
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

void Gen::spike(double x, bool ceiling, int group) {
    if (ceiling) out.add(obj::Spike, x, CORRIDOR_C - 15).kv(key::Rotation, 180).group(group);
    else out.add(obj::Spike, x, 15).group(group);
}

void Gen::portal(size_t i) {
    auto& s = segs[i];
    double x = tl.x(s.t0) + 30;
    if (i > 0 && s.speedRank != segs[i - 1].speedRank) out.add(SPEED_PORTAL[s.speedRank], x, 45);
    if (i == 0 && s.speedRank != 1) out.add(SPEED_PORTAL[s.speedRank], 45, 45);
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

// ---------------------------------------------------------------------------------------
// Cube: path-first generation driven by per-bar rhythm patterns.
// Each bar picks a pattern (jumps, orb hops, pad launches, stairs, pillar runs ...) whose
// clicks sit on beats / off-beats. The player path is simulated from those clicks, then
// platforms are built where the path lands and hazards are placed only where the simulated
// player is guaranteed to clear them.
// ---------------------------------------------------------------------------------------

namespace {
enum ActType { Jump, OrbYellowAct, OrbPinkAct, PadAct };
struct Act { double t; ActType type; int land; }; // land: target surface height in blocks
struct Arc { double t0, y0, v0, tEnd; bool risky; };
struct Flight { std::vector<Arc> arcs; double startH, landH; bool special; };
struct Pillar { double x0, x1, h, t; };

double yOf(Arc const& r, double t) {
    double d = t - r.t0;
    return r.y0 + r.v0 * d - 0.5 * CUBE_G * d * d;
}
// time until an arc starting at y0 with velocity v0 comes down to height h
double fallTime(double y0, double v0, double h) {
    double disc = v0 * v0 + 2 * CUBE_G * (y0 - h);
    if (disc < 0) disc = 0;
    return (v0 + std::sqrt(disc)) / CUBE_G;
}
} // namespace

void Gen::genCube(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double P = beatLen();
    double a = s.t0 + 0.45, b = s.t1 - 0.6;
    if (b <= a) return;
    int diff = int(O.difficulty);

    // ---- 1. choose a rhythm pattern for every bar --------------------------------------
    std::vector<Act> acts;
    int lastTpl = -1;
    for (size_t k = 0; k < beats.size(); k++) {
        if (!down[k] || beats[k] < a - P || beats[k] > b) continue;
        double T[4], H[4];
        for (int i = 0; i < 4; i++) T[i] = beats[k] + i * P, H[i] = T[i] + P / 2;
        float halfOn = std::max(A.onsetAt(mt(H[0]), 0.04), A.onsetAt(mt(H[2]), 0.04));
        double hf = 0.35 + halfOn; // off-beat patterns mostly when the music has off-beats
        bool fastOk = P >= 0.45;   // a full jump fits in one beat
        int lv = s.level;

        struct Tpl { double w; std::vector<Act> acts; };
        std::vector<Tpl> tpl = {
            /*0 two jumps    */ {lv == 0 ? 2.0 : 0.8, {{T[0], Jump, 0}, {T[2], Jump, 0}}},
            /*1 every beat   */ {fastOk ? (lv >= 1 ? 1.2 : 0.4) : 0.0,
                                 {{T[0], Jump, 0}, {T[1], Jump, 0}, {T[2], Jump, 0}, {T[3], Jump, 0}}},
            /*2 orb hop      */ {D.orbs ? 1.3 * hf : 0.0,
                                 {{T[0], Jump, 0}, {H[0], OrbYellowAct, 0}, {T[2], Jump, 0}, {H[2], OrbPinkAct, 0}}},
            /*3 pad + orb    */ {D.orbs && D.pads && lv >= 1 ? 1.1 : 0.0,
                                 {{T[0], PadAct, 0}, {T[1], OrbYellowAct, 0}, {T[3], Jump, 0}}},
            /*4 stairs       */ {diff >= 1 ? 0.9 : 0.5,
                                 diff >= 1 ? std::vector<Act>{{T[0], Jump, 1}, {T[1], Jump, 2}, {T[2], Jump, 1}, {T[3], Jump, 0}}
                                           : std::vector<Act>{{T[0], Jump, 1}, {T[2], Jump, 0}}},
            /*5 orb to step  */ {D.orbs ? 0.9 * hf : 0.0, {{T[0], Jump, 0}, {H[0], OrbYellowAct, 1}, {T[2], Jump, 0}}},
            /*6 orb chain    */ {diff >= 2 && lv >= 2 ? 1.0 * hf : 0.0,
                                 {{T[0], Jump, 0}, {H[0], OrbYellowAct, 0}, {T[1], OrbPinkAct, 0}, {H[1], OrbYellowAct, 0}, {T[3], Jump, 0}}},
            /*7 rest         */ {lv == 0 ? 1.5 : 0.15, {{T[0], Jump, 0}}},
            /*8 pillar run   */ {diff >= 1 && fastOk ? 1.0 : 0.0,
                                 {{T[0], Jump, 1}, {T[1], Jump, 1}, {T[2], Jump, 1}, {T[3], Jump, 0}}},
            /*9 pad to tower */ {D.pads && diff >= 1 ? 0.7 : 0.0, {{T[0], PadAct, 2}, {T[2], Jump, 0}}},
        };
        double total = 0;
        for (size_t i = 0; i < tpl.size(); i++) if (int(i) != lastTpl) total += tpl[i].w;
        double r = rnd() * total;
        int pickI = 0;
        for (size_t i = 0; i < tpl.size(); i++) {
            if (int(i) == lastTpl || tpl[i].w <= 0) continue;
            pickI = int(i);
            if ((r -= tpl[i].w) <= 0) break;
        }
        lastTpl = pickI;
        for (auto& act : tpl[size_t(pickI)].acts) if (act.t >= a && act.t <= b) acts.push_back(act);
    }
    std::sort(acts.begin(), acts.end(), [](auto& l, auto& r) { return l.t < r.t; });

    // ---- 2. simulate the player through the clicks -----------------------------------
    std::vector<Flight> flights;
    std::vector<Pillar> pillars;
    double surf = 0, lastLand = -1e9;
    bool flying = false;
    auto land = [&]() {
        auto& f = flights.back();
        flying = false;
        surf = f.landH;
        lastLand = f.arcs.back().tEnd;
        if (surf > 0) pillars.push_back({tl.x(lastLand) + 5, 1e18, surf, lastLand});
    };
    for (auto& act : acts) {
        if (flying && act.t >= flights.back().arcs.back().tEnd) land();
        bool fromGround = act.type == Jump || act.type == PadAct;
        if (fromGround) {
            if (flying || act.t < lastLand + 0.06) continue;
            bool pad = act.type == PadAct;
            double v0 = CUBE_V * (pad ? PAD_YELLOW : 1.0);
            double apex = surf + v0 * v0 / (2 * CUBE_G);
            double target = act.land * 30.0;
            if (target > apex - 12) target = 0;
            double x = tl.x(act.t);
            if (!pillars.empty() && pillars.back().x1 > 1e17) pillars.back().x1 = x + 10;
            if (pad) out.add(obj::PadYellow, x + 22, surf + 4).group(chunkGroup(si, act.t, x, false));
            else clicks++;
            flights.push_back({{{act.t, surf, v0, act.t + fallTime(surf, v0, target), pad}}, surf, target, pad});
            flying = true;
        } else {
            if (!flying || !D.orbs) continue;
            auto& f = flights.back();
            auto& cur = f.arcs.back();
            if (act.t - cur.t0 < 0.12) continue;
            double y = yOf(cur, act.t);
            if (y < 25 || y > 170) continue;
            bool pink = act.type == OrbPinkAct;
            double v0 = CUBE_V * (pink ? ORB_PINK : ORB_YELLOW);
            double target = act.land * 30.0;
            if (target > y + v0 * v0 / (2 * CUBE_G) - 12) target = 0;
            double x = tl.x(act.t);
            out.add(pink ? obj::OrbPink : obj::OrbYellow, x, y + PLAYER_HALF).group(chunkGroup(si, act.t, x, true));
            cur.tEnd = act.t;
            f.arcs.push_back({act.t, y, v0, act.t + fallTime(y, v0, target), true});
            f.landH = target;
            f.special = true;
            clicks++;
        }
    }
    if (flying) land();
    if (!pillars.empty() && pillars.back().x1 > 1e17) pillars.back().x1 = tl.x(s.t1) - 45;

    // ---- 3. platforms (stacked blocks) ------------------------------------------------
    for (auto& p : pillars) {
        for (double c = p.x0 + 15; c - 15 < p.x1; c += 30) {
            for (double y = 15; y < p.h; y += 30) {
                out.add(obj::Block, c, y).group(chunkGroup(si, p.t, c, false));
            }
        }
    }
    auto inPillar = [&](double sx) {
        for (auto& p : pillars) {
            double right = p.x0 + 30 * std::ceil((p.x1 - p.x0) / 30.0);
            if (sx + 15 > p.x0 - 2 && sx - 15 < right + 2) return true;
        }
        return false;
    };

    // ---- 4. hazards under every flight ----------------------------------------------
    for (auto& fl : flights) {
        double ft0 = fl.arcs.front().t0, ft1 = fl.arcs.back().tEnd;
        double fx0 = tl.x(ft0);
        auto yAtX = [&](double px, double& extra) {
            double t = ft0 + (px - fx0) / v;
            extra = 0;
            if (t <= ft0 || t >= ft1) return 0.0;
            for (auto& r : fl.arcs) {
                if (t >= r.t0 && t <= r.tEnd) {
                    extra = r.risky ? 18 : 0;
                    return std::max(0.0, yOf(r, t));
                }
            }
            return 0.0;
        };
        auto safe = [&](double sx, double top, double half) {
            double mx = D.margin * 0.5;
            for (double px = sx - half - PLAYER_HALF - mx; px <= sx + half + PLAYER_HALF + mx; px += 2) {
                double extra;
                double y = yAtX(px, extra);
                if (y < top + D.margin + extra) return false;
            }
            return true;
        };
        std::vector<std::pair<double, double>> iv;
        double xs = fx0, xe = tl.x(ft1);
        bool in = false;
        double st = 0;
        for (double sx = xs; sx <= xe; sx += 2) {
            bool ok = safe(sx, SPIKE_TOP, SPIKE_HALF) && !inPillar(sx);
            if (ok && !in) in = true, st = sx;
            if (!ok && in) in = false, iv.push_back({st, sx - 2});
        }
        if (in) iv.push_back({st, xe});

        bool pit = fl.special || fl.startH > 0 || fl.landH > 0;
        for (auto& [l, r] : iv) {
            int fit = int(std::floor((r - l) / 30.0)) + 1;
            int n = pit ? std::min(fit, 16) : std::min(fit, 1 + int(rnd() * D.maxSpikes));
            double c = (l + r) / 2;
            double tMid = ft0 + (c - fx0) / v;
            if (!pit && n == 1 && diff >= 2 && rnd() < 0.25 && safe(c, 30, 15)) {
                out.add(obj::Block, c, 15).group(chunkGroup(si, tMid, c, false));
                continue;
            }
            for (int k = 0; k < n; k++) {
                double sx = c + (k - (n - 1) / 2.0) * 30;
                spike(sx, false, chunkGroup(si, tMid, sx, false));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------
// Ball / Spider: gravity flips on the beat inside a 4-block corridor
// ---------------------------------------------------------------------------------------

void Gen::genFlip(size_t si, bool spider) {
    auto& s = segs[si];
    double x0 = tl.x(s.t0) + 75, x1 = tl.x(s.t1) - 15;
    for (double x = x0; x <= x1; x += 30) {
        out.add(obj::Block, x, CORRIDOR_C + 15);
        out.add(obj::Block, x, CORRIDOR_C + 45);
    }
    double a = s.t0 + 0.55, b = s.t1 - 0.6;
    if (b <= a) return;
    double gap = spider ? std::max(D.minGap, 0.2) : std::max(D.minGap * 1.6, 0.45);
    auto ev = pick(candidates(s, a, b, 2), targetCount(s, a, b, spider ? 0.8 : 0.55), gap);
    if (ev.size() % 2 == 1) ev.pop_back(); // must end on the floor
    double late = spider ? D.spiderLate : std::max(0.19, D.ballLate);
    double early = spider ? 0.05 : 0.0;

    auto row = [&](double ta, double tb, bool ceiling) {
        double xa = tl.x(ta) + 18, xb = tl.x(tb) - 18;
        if (ceiling) xa = std::max(xa, x0);
        for (double x = xa; x <= xb; x += 30) spike(x, ceiling, chunkGroup(si, ta, x, ceiling));
    };
    // before the first flip the player must stay on the floor
    if (!ev.empty() && ev.front().t - a > 0.6) row(a, ev.front().t - early, true);
    bool onCeiling = false;
    for (size_t k = 0; k < ev.size(); k++) {
        double tk = ev[k].t;
        double tn = k + 1 < ev.size() ? ev[k + 1].t : s.t1 - 0.4;
        row(tk + late, tn - early, onCeiling); // punish not flipping
        onCeiling = !onCeiling;
        clicks++;
    }
}

// ---------------------------------------------------------------------------------------
// Corridor walls around a path (wave & ship), built from vertically scaled blocks
// ---------------------------------------------------------------------------------------

void Gen::walls2(size_t si, std::vector<double> const& ts, std::vector<double> const& ys, double gap, double xStart, double xEnd) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double sx0 = tl.x(s.t0);
    auto yAt = [&](double t) {
        if (t <= ts.front()) return ys.front();
        if (t >= ts.back()) return ys.back();
        size_t i = size_t(std::upper_bound(ts.begin(), ts.end(), t) - ts.begin());
        double f = (t - ts[i - 1]) / (ts[i] - ts[i - 1]);
        return ys[i - 1] + (ys[i] - ys[i - 1]) * f;
    };
    WallGroups wg{nextGroup++, nextGroup++, si};
    walls.push_back(wg);
    for (double xc = xStart; xc <= xEnd; xc += 30) {
        double lo = 1e9, hi = -1e9;
        for (double px = xc - 15 - 6; px <= xc + 15 + 6; px += 3) {
            double y = yAt(s.t0 + (px - sx0) / v);
            lo = std::min(lo, y), hi = std::max(hi, y);
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

void Gen::genWave(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank]; // normal-size wave: 45 degrees, |dy/dt| == v
    const double ylo = 15, yhi = 210;
    double a = s.t0 + 0.45, b = s.t1 - 0.35;
    if (b <= a) return;
    int sub = int(O.difficulty) >= int(Difficulty::Insane) ? 4 : 2;
    auto all = candidates(s, a, b, sub);
    double minGap = std::max(D.minGap, 0.1);
    auto want = pick(all, targetCount(s, a, b, 1.2), minGap);
    std::set<double> wantT;
    for (auto& e : want) wantT.insert(e.t);

    std::vector<double> ts{s.t0}, ys{ylo};
    double t = s.t0, y = ylo;
    int dir = -1;
    double lastToggle = -1e9;
    double endZone = b - (yhi - ylo) / v * 0.6;
    auto adv = [&](double from, double yy, int d, double to) {
        return std::clamp(yy + d * v * (to - from), ylo, yhi);
    };
    for (size_t i = 0; i < all.size(); i++) {
        double ct = all[i].t;
        if (ct - lastToggle < minGap) continue;
        double yc = adv(t, y, dir, ct);
        double tn = i + 1 < all.size() ? all[i + 1].t : b;
        bool toggle = false;
        if (dir > 0) {
            if (yc + v * (tn - ct) > yhi - 10 || ct > endZone) toggle = true; // forced down
            else if (wantT.count(ct)) toggle = true;
        } else if (ct < endZone && wantT.count(ct)) {
            // only go up if we can come back down at the next candidate in time
            toggle = yc + v * (tn - ct) <= yhi - 10;
        }
        if (!toggle) continue;
        // record the floor contact point (if the wave was sliding) for exact walls
        if (dir < 0 && y - v * (ct - t) < ylo) {
            double tf = t + (y - ylo) / v;
            ts.push_back(tf), ys.push_back(ylo);
        }
        ts.push_back(ct), ys.push_back(yc);
        t = ct, y = yc, dir = -dir, lastToggle = ct;
        clicks++;
    }
    // finish the path
    if (dir < 0) {
        double tf = t + (y - ylo) / v;
        if (tf < s.t1) ts.push_back(tf), ys.push_back(ylo);
        ts.push_back(s.t1 + 1), ys.push_back(ylo);
    } else {
        double tf = t + (yhi - y) / v;
        ts.push_back(tf), ys.push_back(yhi);
        ts.push_back(s.t1 + 1), ys.push_back(yhi);
    }
    walls2(si, ts, ys, D.waveGap, tl.x(s.t0) + 75, tl.x(s.t1) - 30);
}

void Gen::genShip(size_t si) {
    auto& s = segs[si];
    double v = SPEED_UPS[s.speedRank];
    double slope = int(O.difficulty) <= int(Difficulty::Normal) ? 0.45 : 0.65;
    double a = s.t0 + 0.6, b = s.t1 - 0.8;
    std::vector<double> kt{s.t0 + 0.3}, ky{15};
    if (b > a) {
        auto ev = pick(candidates(s, a, b, 1), (b - a) / D.shipSpacing, D.shipSpacing);
        int dir = 1;
        for (auto& e : ev) {
            double dt = e.t - kt.back();
            double maxDy = slope * v * dt / 1.5708;
            double mag = 40 + rnd() * 110;
            double target = std::clamp(ky.back() + dir * std::min(mag, maxDy), 25.0, 200.0);
            if (std::abs(target - ky.back()) < 20) { dir = -dir; continue; }
            kt.push_back(e.t), ky.push_back(target);
            dir = -dir;
        }
    }
    double tEnd = s.t1 - 0.25;
    kt.push_back(tEnd), ky.push_back(15);
    // make sure the descent to the floor at the end is flyable
    for (size_t i = ky.size() - 1; i-- > 1;) {
        double maxY = ky[i + 1] + slope * v * (kt[i + 1] - kt[i]) / 1.5708;
        ky[i] = std::min(ky[i], maxY);
    }
    // cosine-interpolate keypoints into a dense polyline
    std::vector<double> ts, ys;
    for (size_t i = 0; i + 1 < kt.size(); i++) {
        for (int k = 0; k < 16; k++) {
            double u = k / 16.0;
            ts.push_back(kt[i] + (kt[i + 1] - kt[i]) * u);
            ys.push_back(ky[i] + (ky[i + 1] - ky[i]) * (1 - std::cos(3.14159265 * u)) / 2);
        }
    }
    ts.push_back(kt.back()), ys.push_back(ky.back());
    ts.insert(ts.begin(), s.t0), ys.insert(ys.begin(), 15);
    ts.push_back(s.t1 + 1), ys.push_back(15);
    walls2(si, ts, ys, D.shipGap, tl.x(s.t0) + 90, tl.x(s.t1) - 30);
}

// ---------------------------------------------------------------------------------------
// Effects: color, pulse, move (fly-in / breathing walls), alpha, shake, camera
// ---------------------------------------------------------------------------------------

struct RGB { int r, g, b; };
RGB hsv(double h, double s, double v) {
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360;
    double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    return {int((r + m) * 255), int((g + m) * 255), int((b + m) * 255)};
}

void Gen::effects() {
    double P = beatLen();
    double baseHue = rnd() * 360;
    const double laneY = 600;
    auto colorTrig = [&](double x, int channel, RGB c, double dur) {
        out.add(obj::TrigColor, x, laneY).kv(key::Red, c.r).kv(key::Green, c.g).kv(key::Blue, c.b)
            .kv(key::Duration, dur).kv(key::TargetColor, channel).kv(key::Opacity, 1);
    };
    auto bgOf = [&](int lv, size_t i) { return hsv(baseHue + lv * 70 + double(i) * 17, 0.55 + 0.1 * lv, 0.18 + 0.1 * lv); };
    auto gOf = [&](int lv, size_t i) { return hsv(baseHue + lv * 70 + double(i) * 17, 0.6 + 0.1 * lv, 0.1 + 0.06 * lv); };

    colorTrig(1, 1000, bgOf(segs[0].level, 0), 0);
    colorTrig(1, 1001, gOf(segs[0].level, 0), 0);
    colorTrig(1, 1002, {255, 255, 255}, 0);

    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        double x = tl.x(s.t0);
        double dur = s.drop ? 0 : P;
        if (i > 0) {
            colorTrig(x, 1000, bgOf(s.level, i), dur);
            colorTrig(x, 1001, gOf(s.level, i), dur);
        }
        if (s.drop) {
            out.add(obj::TrigShake, x, laneY + 150).kv(key::ShakeStrength, 1 + int(O.difficulty) / 2)
                .kv(key::ShakeInterval, 0.02).kv(key::Duration, 0.45);
        }
        // beat pulses
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 || t >= s.t1) continue;
            if (s.level == 0 && !down[k]) continue;
            if (s.level == 1 && !down[k] && (k % 2)) continue;
            RGB c = hsv(baseHue + s.level * 70 + double(i) * 17, 0.45, 0.35 + 0.15 * s.level + (down[k] ? 0.15 : 0));
            double px = tl.x(t);
            out.add(obj::TrigPulse, px, laneY + 30).kv(key::Red, c.r).kv(key::Green, c.g).kv(key::Blue, c.b)
                .kv(key::FadeIn, 0).kv(key::Hold, 0.02).kv(key::FadeOut, P * (down[k] ? 0.75 : 0.45))
                .kv(key::PulseMode, 0).kv(key::PulseTargetType, 0).kv(key::TargetGroup, 1000);
            if (down[k] && s.level >= 2) {
                out.add(obj::TrigPulse, px, laneY + 60).kv(key::Red, 255).kv(key::Green, 255).kv(key::Blue, 255)
                    .kv(key::FadeIn, 0).kv(key::Hold, 0).kv(key::FadeOut, P * 0.5)
                    .kv(key::PulseMode, 0).kv(key::PulseTargetType, 0).kv(key::TargetGroup, 1002);
            }
        }
        // camera tilt on peak sections
        if (s.level >= 3 && int(O.difficulty) >= int(Difficulty::Hard)) {
            int sign = 1;
            for (double t = s.t0; t < s.t1 - 2 * P; t += 8 * P) {
                out.add(obj::TrigCamRotate, tl.x(t), laneY + 180).kv(key::Degrees, 3 * sign)
                    .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
                sign = -sign;
            }
            out.add(obj::TrigCamRotate, tl.x(s.t1 - P), laneY + 180).kv(key::Degrees, 0)
                .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
        }
    }

    // fly-in obstacles: two move triggers per chunk with opposite offsets (net movement is
    // exactly zero, so the gameplay position never depends on move-unit scaling)
    const double flyDist = 90;
    for (auto& [k, c] : chunks) {
        size_t si = size_t(k >> 21);
        double v = SPEED_UPS[segs[si].speedRank];
        double dur = std::min(0.45, P * 0.5);
        double need = 380 + v * dur;
        double tb = -1;
        for (double bt : beats) {
            double bx = tl.x(bt);
            if (bx <= c.xMin - need && bx >= c.xMin - need - 400) tb = bt;
        }
        if (tb < 0) continue;
        double bx = tl.x(tb);
        int easing = rnd() < 0.5 ? ease::BackOut : ease::BounceOut;
        out.add(obj::TrigMove, bx - 60, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, 0).kv(key::MoveY, -c.dir * flyDist).kv(key::Duration, 0);
        out.add(obj::TrigMove, bx, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, 0).kv(key::MoveY, c.dir * flyDist).kv(key::Duration, dur)
            .kv(key::Easing, easing).kv(key::EasingRate, 2);
        // fade out what the player already passed
        out.add(obj::TrigAlpha, c.xMax + 120, laneY + 120).kv(key::TargetGroup, c.group)
            .kv(key::Duration, P * 0.5).kv(key::Opacity, 0);
    }

    // breathing corridor walls: open on the downbeat, ease back (never narrower than base)
    for (auto& w : walls) {
        auto& s = segs[w.seg];
        if (s.level < 2) continue;
        double v = SPEED_UPS[s.speedRank];
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 + P || t >= s.t1 - P) continue;
            if (!down[k] && s.level < 3) continue;
            double x = tl.x(t);
            out.add(obj::TrigMove, x, laneY + 210).kv(key::TargetGroup, w.top).kv(key::MoveY, 8)
                .kv(key::Duration, 0.05);
            out.add(obj::TrigMove, x + v * 0.06, laneY + 210).kv(key::TargetGroup, w.top).kv(key::MoveY, -8)
                .kv(key::Duration, P * 0.6).kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
            out.add(obj::TrigMove, x, laneY + 240).kv(key::TargetGroup, w.bottom).kv(key::MoveY, -8)
                .kv(key::Duration, 0.05);
            out.add(obj::TrigMove, x + v * 0.06, laneY + 240).kv(key::TargetGroup, w.bottom).kv(key::MoveY, 8)
                .kv(key::Duration, P * 0.6).kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
        }
    }
}

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
    }
    if (O.effects) effects();

    res.objects = out.finish();
    res.objectCount = out.count;
    res.startSpeed = SPEED_ENUM[segs.front().speedRank];

    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "%s | %s | %zu parts (cube %d, ship %d, ball %d, wave %d, spider %d) | ~%d clicks | %d objects",
        A.summary().c_str(), difficultyName(O.difficulty), segs.size(), modeCount[0], modeCount[1],
        modeCount[2], modeCount[3], modeCount[4], clicks, out.count
    );
    res.summary = buf;
    return res;
}

} // namespace

std::vector<int> generatedObjectIds() {
    std::vector<int> ids = {obj::Block, obj::Spike, obj::PadYellow, obj::OrbYellow, obj::OrbPink,
                            obj::PortalCube, obj::PortalShip, obj::PortalBall, obj::PortalWave, obj::PortalSpider,
                            obj::TrigColor, obj::TrigMove, obj::TrigPulse, obj::TrigAlpha, obj::TrigShake, obj::TrigCamRotate};
    for (int id : SPEED_PORTAL) ids.push_back(id);
    return ids;
}

GenResult generate(Analysis const& a, GenOptions const& opt) {
    Gen g(a, opt);
    return g.run();
}

} // namespace rg
