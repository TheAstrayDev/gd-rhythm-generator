#pragma once

// Internal header shared by the generator modules (planning, cube, corridors, effects).
// Object IDs and property keys are GD 2.2 values cross-checked with the gmdbuilder
// mappings (github.com/LXtreme/gmdbuilder, src/gmdbuilder/mappings) and GD docs.

#include "../Generator.hpp"
#include "Style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace rg::gen {

// ---------------------------------------------------------------------------------------
// GD constants
// ---------------------------------------------------------------------------------------

// Speeds ordered by rank (0 = 0.5x .. 4 = 4x), in units per second (1 block = 30 units).
inline constexpr double SPEED_UPS[5] = {251.16, 311.58, 387.42, 468.0, 576.0};
inline constexpr int SPEED_PORTAL[5] = {200, 201, 202, 203, 1334};
inline constexpr int SPEED_ENUM[5] = {1, 0, 2, 3, 4}; // GD's Speed enum for each rank

namespace obj {
inline constexpr int Block = 1, Spike = 8;
inline constexpr int PadYellow = 35, PadPink = 140, PadRed = 1332;
inline constexpr int OrbYellow = 36, OrbPink = 141, OrbRed = 1333, OrbDashGreen = 1704;
inline constexpr int PortalGravityDown = 10, PortalGravityUp = 11, PortalCube = 12, PortalShip = 13;
inline constexpr int PortalMirrorOn = 45, PortalMirrorOff = 46, PortalBall = 47;
inline constexpr int PortalSizeNormal = 99, PortalSizeMini = 101, PortalWave = 660, PortalSpider = 1331;
inline constexpr int PortalRobot = 745, PortalUfo = 111, PortalSwing = 1933, PortalDual = 286, PortalDualOff = 287;
inline constexpr int Teleport = 747; // linked blue + orange pair, the orange one at y + key 54
inline constexpr int OrbBlue = 84, PadBlue = 67, OrbBlack = 1330, OrbSpider = 3004, PadSpider = 3005;
inline constexpr int TrigColor = 899, TrigMove = 901, TrigPulse = 1006, TrigAlpha = 1007;
inline constexpr int TrigToggle = 1049, TrigRotate = 1346, TrigShake = 1520;
inline constexpr int TrigZoom = 1913, TrigCamOffset = 1916, TrigCamRotate = 2015, TrigScale = 2067;
inline constexpr int ShaderOptions = 2904, ShaderShockwave = 2905, ShaderShockline = 2907, ShaderGlitch = 2909;
inline constexpr int ShaderChromatic = 2910, ShaderChromaticGlitch = 2911, ShaderPixelate = 2912;
inline constexpr int ShaderLensCircle = 2913, ShaderRadialBlur = 2914, ShaderMotionBlur = 2915;
inline constexpr int ShaderBulge = 2916, ShaderPinch = 2917, ShaderGray = 2919, ShaderSepia = 2920;
inline constexpr int ShaderInvert = 2921, ShaderHue = 2922;
} // namespace obj

namespace key {
inline constexpr int FlipY = 5, Rotation = 6, Red = 7, Green = 8, Blue = 9, Duration = 10;
inline constexpr int Color1 = 21, Color2 = 22, TargetColor = 23, ZOrder = 25, MoveX = 28, MoveY = 29;
inline constexpr int Easing = 30, Opacity = 35, FadeIn = 45, Hold = 46, FadeOut = 47, PulseMode = 48;
inline constexpr int TargetGroup = 51, PulseTargetType = 52, ActivateGroup = 56, Groups = 57;
inline constexpr int Degrees = 68, Times360 = 69, CenterGroup = 71, ShakeStrength = 75, ShakeInterval = 84;
inline constexpr int EasingRate = 85, NoTouch = 121, ScaleX = 128, ScaleY = 129, PlayerCenter = 138;
inline constexpr int ScaleByX = 150, ScaleByY = 151, Zoom = 371, TeleportOffset = 54, FlipX = 4;
// shader trigger properties (meaning depends on the shader type)
inline constexpr int ShSpeed = 175, ShStrength = 176, ShTimeOffset = 177, ShWidth = 179, ShThickness = 180;
inline constexpr int ShFadeIn = 181, ShFadeOut = 182, ShUseX = 188, ShTargetY = 189, ShUseY = 190;
} // namespace key

namespace ease {
inline constexpr int None = 0, InOut = 1, In = 2, Out = 3, ElasticOut = 6, BounceOut = 9;
inline constexpr int ExpIn = 11, ExpOut = 12, SineInOut = 13, SineOut = 15, BackOut = 18;
}

// Color channels used by the generator.
namespace ch {
inline constexpr int BG = 1000, Ground = 1001, Line = 1002, Ground2 = 1009, Middle = 1013;
inline constexpr int Blocks = 1, Accent = 2, Deco = 3, DecoGlow = 4;
}

// ---------------------------------------------------------------------------------------
// Player physics, reconstructed from GD's own code (GD-Remake / OSGD player physics,
// cross-checked with the GD wiki). GD runs 240 steps per second on native values:
// velocity x 54 = units / s, gravity x 2916 = units / s^2. Units: 1 block = 30.
// ---------------------------------------------------------------------------------------
struct CubePhys { double v, g; }; // jump velocity, gravity
inline constexpr CubePhys CUBE_PHYS[5] = {
    {10.620032 * 54, 0.940199 * 2916}, // 0.5x: apex 60, airtime 0.42 s
    {11.180032 * 54, 0.958199 * 2916}, // 1x  : apex 65, airtime 0.43 s
    {11.420032 * 54, 0.957199 * 2916}, // 2x  : apex 68
    {11.230032 * 54, 0.961199 * 2916}, // 3x
    {11.230032 * 54, 0.961199 * 2916}, // 4x
};
inline constexpr double FALL_MAX = 810;   // terminal fall speed (cube, ball, robot, spider)
inline constexpr double ORB_YELLOW = 1.0, ORB_PINK = 0.72, ORB_RED = 1.38; // x the jump velocity
inline constexpr double ORB_BLUE = 0.4;   // towards the new floor (0.8, halved by the flip)
inline constexpr double ORB_BLACK = 810;  // a black orb slams straight down
inline constexpr double PAD_V = 864;      // pads push with PAD_V x bump
inline constexpr double PAD_YELLOW = 1.0, PAD_PINK = 0.65, PAD_RED = 1.25, PAD_BLUE = 0.4;
// ball: 0.6 x the cube's gravity, a click flips it with 181 units / s towards the new floor
inline constexpr double BALL_GRAV = 0.6, BALL_PUSH = 181;
// robot: half the cube's jump velocity, held flat (no gravity) for up to 0.28 s, then 0.9 x gravity
inline constexpr double ROBOT_JUMP = 0.5, ROBOT_GRAV = 0.9, ROBOT_HOLD = 0.28;
// ufo: a click sets the climb speed to 378 (mini 367); gravity 1676 while rising fast, else 1117
inline constexpr double UFO_CLICK = 378, UFO_CLICK_MINI = 367, UFO_G_FAST = 1676, UFO_G_SLOW = 1117, UFO_FALL = 345.6;
// swing: gravity 1118 (mini 1676), +-432 units / s, a click flips gravity keeping 0.8 of the speed
inline constexpr double SWING_G = 1118, SWING_G_MINI = 1676, SWING_MAX = 432, SWING_KEEP = 0.8;
// how far apart the floor and ceiling of a flying mode are (the portal sets them)
inline constexpr double FLY_BOUND = 300;

// A ballistic flight with a terminal fall speed (cube, orbs, pads, ball, robot after release).
struct Ballistic {
    double g, vmax = FALL_MAX;
    double y(double y0, double v0, double d) const {
        if (v0 <= -vmax) return y0 - vmax * d;
        double tc = (v0 + vmax) / g; // the fall speed caps here
        if (d <= tc) return y0 + v0 * d - 0.5 * g * d * d;
        return y0 + v0 * tc - 0.5 * g * tc * tc - vmax * (d - tc);
    }
    double v(double v0, double d) const { return v0 <= -vmax ? -vmax : std::max(v0 - g * d, -vmax); }
    double apex(double y0, double v0) const { return v0 > 0 ? y0 + v0 * v0 / (2 * g) : y0; }
    // time until the flight comes down to height h (after the apex)
    double fall(double y0, double v0, double h) const {
        if (v0 <= -vmax) return std::max(0.0, (y0 - h) / vmax);
        double ta = std::max(0.0, v0 / g), D = apex(y0, v0) - h;
        if (D <= 0) return ta;
        double u = std::max(0.0, -v0); // downward speed at the apex (or at the start)
        double t1 = (vmax - u) / g, d1 = u * t1 + 0.5 * g * t1 * t1;
        if (D <= d1) return ta + (-u + std::sqrt(u * u + 2 * g * D)) / g;
        return ta + t1 + (D - d1) / vmax;
    }
};

inline constexpr double SPIKE_TOP = 22.0;  // top of a ground spike's hitbox
inline constexpr double SPIKE_HALF = 3.0;  // half width of a spike's hitbox
inline constexpr double PLAYER_HALF = 15.0;

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

inline constexpr DiffCfg DIFFS[DIFFICULTY_COUNT] = {
    /* Easy   */ {1, 1, 0.35, 0.55, 1, 14, 110, 165, 1.10, 0.30, 0.16, false, true, false, false},
    /* Normal */ {1, 2, 0.55, 0.40, 2, 11, 92, 140, 0.90, 0.26, 0.13, true, true, true, false},
    /* Hard   */ {2, 2, 0.80, 0.28, 2, 8, 78, 120, 0.75, 0.22, 0.10, true, true, true, true},
    /* Harder */ {2, 3, 1.00, 0.22, 3, 6, 68, 108, 0.62, 0.20, 0.08, true, true, true, true},
    /* Insane */ {3, 3, 1.30, 0.17, 3, 4, 58, 96, 0.52, 0.18, 0.07, true, true, true, true},
    /* Demon  */ {3, 4, 1.60, 0.13, 3, 3, 50, 86, 0.45, 0.17, 0.06, true, true, true, true},
};

inline constexpr double LEVEL_DENSITY[4] = {0.55, 0.8, 1.0, 1.25};

// modes that fly between a floor and a ceiling (corridor modes, capped at 3x speed)
inline bool flyingMode(Mode m) { return m == Mode::Ship || m == Mode::Wave || m == Mode::Ufo || m == Mode::Swing; }

// Block themes. Every tile has a full 30x30 block hitbox and every slope a 30x30 slope hitbox
// (checked against GD's hitbox table); the slope is the one GD's editor lists for that set.
struct TileSet { int top, outer, inner, pillar, cap, slope; };
inline constexpr TileSet TILESETS[TILESET_COUNT] = {
    /* grid     */ {2, 3, 83, 7, 6, 1743},
    /* beveled  */ {70, 71, 94, 75, 74, 1745},
    /* mech     */ {77, 78, 76, 82, 81, 305},
    /* colored  */ {208, 209, 207, 213, 212, 1906},
    /* outline  */ {467, 469, 467, 471, 470, 1338},
    /* black    */ {91, 92, 94, 96, 95, 1747},
};
inline constexpr int FILL_BLOCK = 94;                      // plain black: stretches without showing it
inline constexpr int GLOW_LARGE = 1011, GLOW_MEDIUM = 503; // straight glow, bright edge at the bottom

// ---------------------------------------------------------------------------------------
// Object string writer
// ---------------------------------------------------------------------------------------

inline std::string num(double v) {
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

    // where gameplay objects are (x, weight): the effects director keeps the screen calm
    // when the player has a lot to read
    std::vector<std::pair<double, float>> heat;

    static float heatOf(int id) {
        switch (id) {
            case 8: case 1705: case 1706: case 1707: case 186: case 187: case 188: return 1.f;  // hazards
            case 1: return 0.1f;
            case 35: case 36: case 67: case 84: case 140: case 141: case 1332: case 1333: case 1704:
            case 1330: case 3004: case 3005: return 1.5f;                                        // orbs, pads
            case 10: case 11: case 12: case 13: case 47: case 99: case 101: case 660: case 1331: case 745:
            case 111: case 1933: case 286: case 287: case 747: return 1.f;                       // portals
            default: return 0.f;
        }
    }
    Out& add(int id, double x, double y) {
        float w = heatOf(id);
        if (w > 0) heat.push_back({x, w});
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
    Out& groups(int a, int b) {
        if (a > 0 && b > 0) str += ",57," + std::to_string(a) + '.' + std::to_string(b);
        else group(a > 0 ? a : b);
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
    // optional table measured from GD (x at t = i * step); used instead of the model
    std::vector<double> table;
    double step = 0.01;

    K const& at(double t) const {
        size_t i = 0;
        while (i + 1 < ks.size() && ks[i + 1].t <= t) i++;
        return ks[i];
    }
    double model(double t) const {
        auto& k = at(t);
        return k.x + (t - k.t) * k.v;
    }
    double x(double t) const {
        if (table.size() < 2) return model(t);
        double f = t / step;
        if (f <= 0) return table.front() + t * (table[1] - table[0]) / step;
        size_t i = size_t(f);
        if (i + 1 >= table.size()) {
            size_t n = table.size();
            return table[n - 1] + (t - (n - 1) * step) * (table[n - 1] - table[n - 2]) / step;
        }
        double u = f - double(i);
        return table[i] * (1 - u) + table[i + 1] * u;
    }
};

// A part of the level with one gamemode and speed.
struct Seg {
    double t0, t1;
    Mode mode;
    int speedRank;
    int level;
    bool drop;
    bool mini = false;     // mini size portal
    bool mirror = false;   // mirror portal (visual)
    bool gravity = false;  // gravity flip inside (ship / wave)
    bool build = false;    // energy rises into the next part
    int style = 0;         // per-part effect style (palette / shader set)
    double corridor = 120; // ball / spider ceiling height
    bool dual = false;     // dual portal: two icons (wave: mirrored, cube: floor + ceiling)
    int section = 0;       // which song section the part belongs to
    Genome g;              // this part's variation of the level style
    int tileset = 0;       // block design: one theme per song section
};

// A saw half-buried in a corridor wall at time t (top or bottom wall), its edge on the line
// the wall would have had.
struct Bite { double t; bool top; int id; double r; };
// One path through a corridor: times, centre heights and the free height around it.
struct Lane {
    std::vector<double> ts, ys, gaps;
    double half = 15; // the player's half hitbox (for the trace)
    std::vector<Bite> bites;
    void add(double t, double y, double gap) { ts.push_back(t), ys.push_back(y), gaps.push_back(gap); }
};
// One lane per stretch of a path between teleports (idx[k]: first point after teleport k, which
// moved the player by dys[k]). GD teleports as soon as the player's box touches the portal, a bit
// before the planned moment: the old lane runs on past the portal and the new one starts early
// (the path shifted by dy).
inline std::vector<Lane> splitAtTeleports(std::vector<double> const& ts, std::vector<double> const& ys,
                                          std::vector<double> const& gs, std::vector<size_t> const& idx,
                                          std::vector<double> const& dys, double half) {
    std::vector<Lane> lanes(1);
    size_t k = 0;
    for (size_t i = 0; i < ts.size(); i++) {
        if (k < idx.size() && i == idx[k]) {
            double tt = ts[i], dy = dys[k];
            for (size_t j = i; j < ts.size() && ts[j] <= tt + 0.06; j++) lanes.back().add(ts[j], ys[j] - dy, gs[j]);
            lanes.emplace_back();
            size_t j0 = i;
            while (j0 > 0 && ts[j0 - 1] >= tt - 0.15) j0--;
            for (size_t j = j0; j < i; j++) lanes.back().add(ts[j], ys[j] + dy, gs[j]);
            k++;
        }
        lanes.back().add(ts[i], ys[i], gs[i]);
    }
    for (auto& L : lanes) L.half = half;
    return lanes;
}
// saws by size, as (id, radius); the outline blades are the same sizes
inline constexpr std::pair<int, double> SAWS[3] = {{1707, 12}, {1706, 21.6}, {1705, 32.3}};
inline constexpr std::pair<int, double> BLADES[3] = {{188, 12.6}, {187, 22}, {186, 32.3}};

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

// top / bottom wall of a corridor (they "breathe" on the beat; a dual divider never moves)
struct WallGroups { int top, bottom; size_t seg; double topEnd = -1e18, bottomEnd = -1e18; }; // last column of each wall

class Gen {
public:
    Gen(Analysis const& a, GenOptions const& o) : A(a), O(o), D(DIFFS[int(o.difficulty)]), rng(o.seed) {}

    GenResult run();

    // ---- shared state (used by all modules) -------------------------------------------
    Analysis const& A;
    GenOptions const& O;
    DiffCfg const& D;
    std::mt19937 rng;
    Out out;
    Timeline tl;
    std::vector<Seg> segs;
    std::vector<double> beats;   // level time
    std::vector<bool> down;
    std::vector<size_t> beatIdx; // index into the analysis arrays for each level beat
    int nextGroup = 1;
    std::map<long, Chunk> chunks;
    std::vector<WallGroups> walls;
    int modeCount[MODE_COUNT] = {};
    Genome G;                  // the level's style genome (a new blend every generation)
    std::string styleDesc;
    // design layer: every solid block cell the gameplay placed (autotiled in design())
    struct Cell { double x, y; int group; size_t seg; };
    std::map<std::pair<long, long>, Cell> cells;
    int glowGroup = 0;   // every edge glow (lights up with the vocal pool)
    bool glowOn = true;  // the level glows along its edges (one decision for the whole level)
    std::map<long, std::vector<std::pair<double, double>>> hazards; // spikes by x / 30
    bool nearHazard(double x, double y, double r = 30) const {
        for (long k = std::lround(x / 30) - 1; k <= std::lround(x / 30) + 1; k++) {
            auto it = hazards.find(k);
            if (it == hazards.end()) continue;
            for (auto& [hx, hy] : it->second)
                if (std::abs(hx - x) < r && std::abs(hy - y) < r) return true;
        }
        return false;
    }
    int clicks = 0;
    std::vector<TracePoint> trace;       // the intended player paths (tests check them)
    std::map<std::string, int> features; // what the generator used (for the summary)
    double patternSpace = 0;             // log10 of the per-bar combination count
    std::map<char, int> styleUse;        // how often each cube action letter was used so far
    int styleTotal = 0;

    double lt(double musicT) const { return musicT + O.syncOffset - O.songOffset; }
    double mt(double levelT) const { return levelT - O.syncOffset + O.songOffset; }
    double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    int rndInt(int n) { return n <= 1 ? 0 : int(rnd() * n) % n; }
    double beatLen() const { return A.beatPeriod; }
    int diff() const { return int(O.difficulty); }
    void used(char const* feature) { features[feature]++; }

    // ---- planning (Plan.cpp) ---------------------------------------------------------
    void planSegments();
    void buildTimeline();
    std::vector<Cand> candidates(Seg const& s, double a, double b, int sub) const;
    std::vector<Cand> pick(std::vector<Cand> c, double count, double minGap) const;
    double targetCount(Seg const& s, double a, double b, double mult = 1) const;
    int chunkGroup(size_t segIdx, double t, double x, bool upper);
    void spike(double x, double y, bool down, int group);
    void portal(size_t i);
    void solidBlock(double x, double y, int group, size_t seg);
    void design();
    void genRobot(size_t i);
    void speedPortals();
    void planSpeedNet();
    std::vector<double> speedNet; // times of the safety-net speed columns (planned before generation)
    bool nearSpeedNet(double t, double tol) const {
        for (double n : speedNet) if (std::abs(n - t) < tol) return true;
        return false;
    }
    void portalEnd(size_t i);
    // music helpers
    float kickAt(double levelT) const;
    float snareAt(double levelT) const;
    float sustainAt(double levelT) const;

    // ---- gameplay (Cube.cpp / Corridor.cpp / Flyer.cpp / Dual.cpp) -------------------------
    void genCube(size_t i);
    void genFlip(size_t i, bool spider);
    void genWave(size_t i);
    void genShip(size_t i);
    void genFlyer(size_t i, bool swing); // ufo / swing
    void genDualWave(size_t i);
    void genDualCube(size_t i);
    // walls around one or more lanes: slopes where the path runs at 45 degrees, the part's
    // tileset on every face, glow along the faces
    void corridor(size_t i, std::vector<Lane> const& lanes, double xStart, double xEnd);
    void walls2(size_t i, std::vector<double> const& ts, std::vector<double> const& ys,
                std::vector<double> const& gaps, double xStart, double xEnd) {
        corridor(i, {Lane{ts, ys, gaps}}, xStart, xEnd);
    }
    // a saw / blade (round hazard) of radius r
    void saw(int id, double x, double y, int group);

    // ---- effects (Effects.cpp) -------------------------------------------------------
    void effects();
};

} // namespace rg::gen
