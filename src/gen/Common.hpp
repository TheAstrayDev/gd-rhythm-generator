#pragma once

// Internal header shared by the generator modules (planning, cube, corridors, effects).
// Object IDs and property keys are GD 2.2 values cross-checked with the gmdbuilder
// mappings (github.com/LXtreme/gmdbuilder, src/gmdbuilder/mappings) and GD docs.

#include "../Generator.hpp"
#include "Style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
inline constexpr int ScaleByX = 150, ScaleByY = 151, Zoom = 371;
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

// Approximate GD player physics (units, seconds). Vertical physics does not depend on speed.
inline constexpr double CUBE_G = 3449.0;   // gravity
inline constexpr double CUBE_V = 670.8;    // jump velocity  -> ~65 units high, ~0.39 s airtime
inline constexpr double ORB_YELLOW = 1.0;  // multipliers of CUBE_V
inline constexpr double ORB_PINK = 0.72;  // ~8 / 11.18
inline constexpr double ORB_RED = 1.35;   // ~15.1 / 11.18 (GD per-tick jump velocities)
inline constexpr double PAD_YELLOW = 1.43; // ~16 / 11.18
inline constexpr double PAD_PINK = 0.93;   // ~10.4 / 11.18
inline constexpr double PAD_RED = 1.79;    // ~20 / 11.18
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

    Out& add(int id, double x, double y) {
        float w = id == 8 ? 1.f : id == 1 ? 0.1f : (id == 35 || id == 36 || id == 67 || id == 84 || id == 140 ||
                  id == 141 || id == 1332 || id == 1333 || id == 1704) ? 1.5f
                  : (id == 10 || id == 11 || id == 12 || id == 13 || id == 47 || id == 99 || id == 101 ||
                     id == 660 || id == 1331) ? 1.f : 0.f;
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
    Genome g;              // this part's variation of the level style
    int tileset = 0, tilesetAlt = 0; // block design (two sets blended per part)
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
    int modeCount[6] = {0, 0, 0, 0, 0, 0};
    Genome G;                  // the level's style genome (a new blend every generation)
    std::string styleDesc;
    // design layer: every solid block cell the gameplay placed, and corridor wall faces
    struct Cell { double x, y; int group; size_t seg; };
    std::map<std::pair<long, long>, Cell> cells;
    struct WallFace { double x, bottomTop, topBottom; size_t seg; int groupTop, groupBottom; };
    std::vector<WallFace> wallFaces;
    int glowGroup = 0; // every edge glow (lights up with the vocal pool)
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
    void portalEnd(size_t i);
    // music helpers
    float kickAt(double levelT) const;
    float snareAt(double levelT) const;
    float sustainAt(double levelT) const;

    // ---- gameplay (Cube.cpp / Corridor.cpp) ---------------------------------------------
    void genCube(size_t i);
    void genFlip(size_t i, bool spider);
    void genWave(size_t i);
    void genShip(size_t i);
    void walls2(size_t i, std::vector<double> const& ts, std::vector<double> const& ys,
                std::vector<double> const& gaps, double xStart, double xEnd);

    // ---- effects (Effects.cpp) -------------------------------------------------------
    void effects();
};

} // namespace rg::gen
