// Offline test for the analyzer + generator (no GD needed).
// Builds a synthetic EDM-like track with known tempo and structure, or loads a 16-bit WAV.
//   offline_test                -> synthetic 128 BPM track
//   offline_test song.wav       -> real song (16-bit PCM wav)

#include "../src/Analysis.hpp"
#include "../src/Generator.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace rg;

static std::vector<float> synth(double bpm, double phase, int sr, double& dur) {
    // structure in bars: intro 16 (quiet), build 8, drop 16 (loud), break 8 (quiet), drop 16, outro 8
    struct Part { int bars; float kick, hat, pad; };
    std::vector<Part> parts = {{16, 0.0f, 0.15f, 0.25f}, {8, 0.5f, 0.3f, 0.3f}, {16, 1.0f, 0.6f, 0.5f},
                               {8, 0.0f, 0.1f, 0.3f},  {16, 1.0f, 0.7f, 0.6f}, {8, 0.4f, 0.2f, 0.2f}};
    double beat = 60.0 / bpm;
    int totalBars = 0;
    for (auto& p : parts) totalBars += p.bars;
    dur = phase + totalBars * 4 * beat + 1;
    std::vector<float> x(size_t(dur * sr), 0);
    std::mt19937 rng(7);
    std::normal_distribution<float> n(0, 1);
    int bar = 0;
    for (auto& p : parts) {
        for (int b = 0; b < p.bars; b++, bar++) {
            for (int k = 0; k < 4; k++) {
                double t = phase + (bar * 4 + k) * beat;
                size_t s0 = size_t(t * sr);
                // kick
                for (size_t i = 0; i < size_t(0.15 * sr) && s0 + i < x.size(); i++) {
                    double tt = double(i) / sr;
                    x[s0 + i] += p.kick * 0.8f * float(std::sin(2 * M_PI * (50 + 100 * std::exp(-tt * 30)) * tt) * std::exp(-tt * 18));
                }
                // snare-ish on 2 and 4 in loud parts
                if (k % 2 == 1 && p.kick > 0.6f)
                    for (size_t i = 0; i < size_t(0.1 * sr) && s0 + i < x.size(); i++)
                        x[s0 + i] += 0.35f * n(rng) * float(std::exp(-double(i) / sr * 35));
                // hats on offbeats
                size_t h0 = size_t((t + beat / 2) * sr);
                for (size_t i = 0; i < size_t(0.04 * sr) && h0 + i < x.size(); i++)
                    x[h0 + i] += p.hat * 0.3f * n(rng) * float(std::exp(-double(i) / sr * 90));
            }
            // pad chord for the whole bar
            double t0 = phase + bar * 4 * beat;
            for (size_t i = size_t(t0 * sr); i < size_t((t0 + 4 * beat) * sr) && i < x.size(); i++) {
                double tt = double(i) / sr;
                x[i] += p.pad * 0.08f * float(std::sin(2 * M_PI * 220 * tt) + std::sin(2 * M_PI * 277.2 * tt) + std::sin(2 * M_PI * 329.6 * tt));
            }
        }
    }
    // a "singer": held harmonic notes with vibrato, syllables deliberately OFF the beat grid,
    // during the build and both drops - the beat tracker must ignore them
    {
        double vbar0[] = {16, 40, 64}, vbars[] = {8, 16, 16};
        std::uniform_real_distribution<double> u(0, 1);
        static const double scale[] = {220, 246.9, 261.6, 293.7, 329.6, 349.2, 392};
        for (int part = 0; part < 3; part++) {
            double t = phase + vbar0[part] * 4 * beat + 0.13;
            double end = phase + (vbar0[part] + vbars[part]) * 4 * beat;
            while (t < end) {
                double len = 0.18 + u(rng) * 0.6;
                double f0 = scale[int(u(rng) * 7)];
                size_t s0 = size_t(t * sr), s1 = std::min(x.size(), size_t((t + len) * sr));
                for (size_t i = s0; i < s1; i++) {
                    double tt = double(i - s0) / sr;
                    double env = std::min(1.0, tt / 0.03) * std::min(1.0, (len - tt) / 0.05);
                    double f = f0 * (1 + 0.01 * std::sin(2 * M_PI * 5.5 * tt));
                    double v = 0;
                    for (int h = 1; h <= 6; h++) v += std::sin(2 * M_PI * f * h * tt) / h;
                    x[i] += float(0.12 * env * v);
                }
                t += len + 0.05 + u(rng) * 0.25;
            }
        }
    }
    for (auto& v : x) v += 0.01f * n(rng);
    return x;
}

static bool loadWav(char const* path, std::vector<float>& out, int& sr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4)) return false;
    size_t p = 12;
    int ch = 1, bits = 16;
    while (p + 8 <= d.size()) {
        uint32_t len;
        std::memcpy(&len, &d[p + 4], 4);
        if (!std::memcmp(&d[p], "fmt ", 4)) {
            ch = *(int16_t*)&d[p + 10];
            sr = *(int32_t*)&d[p + 12];
            bits = *(int16_t*)&d[p + 22];
        } else if (!std::memcmp(&d[p], "data", 4)) {
            if (bits != 16) return false;
            size_t n = std::min<size_t>(len, d.size() - p - 8) / 2 / ch;
            out.resize(n);
            for (size_t i = 0; i < n; i++) {
                float s = 0;
                for (int c = 0; c < ch; c++) s += *(int16_t*)&d[p + 8 + (i * ch + c) * 2] / 32768.f;
                out[i] = s / ch;
            }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

struct ObjStat { int id; double x, y; std::map<int, double> kv; };

static std::vector<ObjStat> parse(std::string const& s) {
    std::vector<ObjStat> res;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        std::stringstream is(item);
        std::string k, v;
        ObjStat o{};
        while (std::getline(is, k, ',') && std::getline(is, v, ',')) {
            int key = std::stoi(k);
            double val = std::stod(v);
            if (key == 1) o.id = int(val);
            else if (key == 2) o.x = val;
            else if (key == 3) o.y = val;
            else o.kv[key] = val;
        }
        res.push_back(o);
    }
    return res;
}

int main(int argc, char** argv) {
    std::vector<float> pcm;
    int sr = 44100;
    double trueBpm = 128, truePhase = 0.37, dur = 0;
    bool synthetic = argc < 2 || std::strncmp(argv[1], "synth", 5) == 0;
    if (argc >= 2 && synthetic && argv[1][5]) trueBpm = std::atof(argv[1] + 5);
    if (synthetic) pcm = synth(trueBpm, truePhase, sr, dur);
    else if (!loadWav(argv[1], pcm, sr)) { std::printf("cannot read %s\n", argv[1]); return 1; }

    auto a = analyze(pcm, sr);
    std::printf("Analysis: %s\n", a.summary().c_str());
    for (auto& s : a.sections)
        std::printf("  section %6.2f-%6.2f level %d energy %.2f%s bars %d\n", s.start, s.end, s.level, s.energy, s.drop ? " DROP" : "", s.bars);

    int fails = 0;
    if (synthetic) {
        double err = std::abs(a.bpm - trueBpm);
        double beat = 60 / trueBpm;
        double phaseErr = 1e9;
        for (double b : a.beats) {
            double d = std::fmod(b - truePhase + 100 * beat, beat);
            phaseErr = std::min(phaseErr, std::min(d, beat - d));
            break;
        }
        // mean beat alignment error over the whole song
        double sum = 0;
        for (double b : a.beats) {
            double d = std::fmod(b - truePhase + 100 * beat, beat);
            sum += std::min(d, beat - d);
        }
        double mean = sum / std::max<size_t>(1, a.beats.size());
        double signedSum = 0;
        for (double b : a.beats) {
            double d = std::fmod(b - truePhase + 100 * beat, beat);
            signedSum += d > beat / 2 ? d - beat : d;
        }
        std::printf("signed mean beat offset %+.1f ms (positive = detected late)\n", signedSum / a.beats.size() * 1000);
        std::printf("BPM error %.3f, first beat phase error %.1f ms, mean beat error %.1f ms\n", err, phaseErr * 1000, mean * 1000);
        if (err > 0.5) { std::printf("FAIL: tempo\n"); fails++; }
        if (mean > 0.03) { std::printf("FAIL: beat alignment\n"); fails++; }
        // downbeats should be at bar starts
        int good = 0, total = 0;
        for (size_t i = 0; i < a.beats.size(); i++) if (a.downbeat[i]) {
            total++;
            double barPos = std::fmod(a.beats[i] - truePhase + 1000 * 4 * beat, 4 * beat);
            if (barPos < 0.05 || 4 * beat - barPos < 0.05) good++;
        }
        std::printf("downbeats on bar starts: %d/%d\n", good, total);
        if (good < total * 0.9) { std::printf("FAIL: downbeat phase\n"); fails++; }
    }

    for (int d = 0; d < DIFFICULTY_COUNT; d++) {
        GenOptions o;
        o.difficulty = Difficulty(d);
        // RG_SEED / RG_STYLE widen the sweep: another seed base, a fixed style mask
        o.seed = 12345 + d + (std::getenv("RG_SEED") ? uint32_t(std::atoi(std::getenv("RG_SEED"))) : 0);
        if (char const* st = std::getenv("RG_STYLE")) o.styleMask = uint32_t(std::atoi(st));
        auto r = generate(a, o);
        auto objs = parse(r.objects);
        std::map<int, int> byId;
        int bad = 0;
        for (auto& ob : objs) {
            byId[ob.id]++;
            if (!std::isfinite(ob.x) || !std::isfinite(ob.y) || ob.x < -10 || ob.y < -200 || ob.y > 1500) bad++;
        }
        std::printf("\n[%s] %s\n  features: %s\n  ids:", difficultyName(Difficulty(d)), r.summary.c_str(), r.features.c_str());
        for (auto& [id, c] : byId) std::printf(" %d:%d", id, c);
        std::printf("\n");
        if (bad) { std::printf("FAIL: %d objects with bad coordinates\n", bad); fails++; }
        if (int(objs.size()) != r.objectCount) { std::printf("FAIL: count mismatch %zu vs %d\n", objs.size(), r.objectCount); fails++; }
        if (r.objectCount < 50) { std::printf("FAIL: too few objects\n"); fails++; }
        // sync sensitivity: if the game applied a speed portal d units off from where the
        // timeline assumes, the timing error at any moment is d * (1/v0 - 1/v) (it telescopes,
        // it does not grow with the number of portals). Report it for d = 16 units.
        {
            std::map<int, double> ups = {{200, 251.16}, {201, 311.58}, {202, 387.42}, {203, 468.0}, {1334, 576.0}};
            std::map<long, double> stacks;
            for (auto& ob : objs) if (ups.count(ob.id)) stacks[std::lround(ob.x)] = ups[ob.id];
            double worst = 0;
            int changes = 0;
            double prev = 311.58;
            for (auto& [x, sp] : stacks) {
                if (std::abs(sp - prev) > 1) changes++;
                prev = sp;
                worst = std::max(worst, 16 * std::abs(1 / 311.58 - 1 / sp));
            }
            std::printf("  sync: %d speed changes, error if portals fired 16 units off: %.1f ms\n", changes, worst * 1000);
        }
        // playthrough check: the paths the generator intends the player to take (cube flights,
        // dual cubes, every corridor lane) must not touch a solid block, a slope, a spike or a saw
        {
            static const std::set<int> slopeIds = {1743, 1745, 305, 1906, 1338, 1747};
            static const std::map<int, double> sawR = {{1705, 32.3}, {1706, 21.6}, {1707, 12}, {186, 32.3}, {187, 22}, {188, 12.6}};
            static const std::set<int> solidIds = {1,  2,  3,  83, 7,  6,  70,  71,  94,  75,  74,  77,  78,  76,  82, 81,
                                                   208, 209, 207, 213, 212, 467, 469, 471, 470, 91, 92, 96, 95};
            struct Box { double x0, x1, y0, y1; };
            struct Tri { double x0, y0; bool fx, fy; };
            struct Saw { double x, y, r; };
            std::map<long, std::vector<Box>> boxes, spikes;
            std::map<long, std::vector<Tri>> tris;
            std::map<long, std::vector<Saw>> saws;
            auto flag = [](ObjStat const& o, int k) { auto it = o.kv.find(k); return it != o.kv.end() && it->second != 0; };
            for (auto& ob : objs) {
                long k = std::lround(ob.x / 30);
                if (solidIds.count(ob.id) && !flag(ob, 121)) {
                    double h = 30 * (ob.kv.count(129) ? ob.kv.at(129) : 1);
                    boxes[k].push_back({ob.x - 15, ob.x + 15, ob.y - h / 2, ob.y + h / 2});
                } else if (slopeIds.count(ob.id)) {
                    tris[k].push_back({ob.x - 15, ob.y - 15, flag(ob, 4), flag(ob, 5)});
                } else if (ob.id == 8) {
                    spikes[k].push_back({ob.x - 3, ob.x + 3, ob.y - 6, ob.y + 6});
                } else if (sawR.count(ob.id)) {
                    saws[k].push_back({ob.x, ob.y, sawR.at(ob.id)});
                }
            }
            int hits = 0;
            for (auto& tp : r.trace) {
                // blocks kill only through GD's inner hitbox (a touch on the outer one lands the
                // player on top or slides it along), hazards use the full hitbox
                double hs = tp.half >= 15 ? 9 : tp.half - 0.5, hh = tp.half - 2;
                long k0 = std::lround(tp.x / 30);
                char const* what = nullptr;
                for (long k = k0 - 2; k <= k0 + 2 && !what; k++) {
                    for (auto& b : boxes[k])
                        if (tp.x + hs > b.x0 && tp.x - hs < b.x1 && tp.y + hs > b.y0 && tp.y - hs < b.y1) what = "block";
                    for (auto& t : tris[k]) {
                        double u0 = tp.x - hs - t.x0, u1 = tp.x + hs - t.x0, w0 = tp.y - hs - t.y0, w1 = tp.y + hs - t.y0;
                        if (t.fx) std::swap(u0, u1), u0 = 30 - u0, u1 = 30 - u1;
                        if (t.fy) std::swap(w0, w1), w0 = 30 - w0, w1 = 30 - w1;
                        u0 = std::max(u0, 0.0), u1 = std::min(u1, 30.0), w0 = std::max(w0, 0.0), w1 = std::min(w1, 30.0);
                        if (u0 < u1 && w0 < w1 && w0 < u1) what = "slope";
                    }
                    for (auto& b : spikes[k])
                        if (tp.x + hh > b.x0 && tp.x - hh < b.x1 && tp.y + hh > b.y0 && tp.y - hh < b.y1) what = "spike";
                    for (auto& c : saws[k]) {
                        double dx = std::max(0.0, std::abs(c.x - tp.x) - hh), dy = std::max(0.0, std::abs(c.y - tp.y) - hh);
                        if (dx * dx + dy * dy < c.r * c.r) what = "saw";
                    }
                }
                if (what) {
                    if (hits < 6) std::printf("    path touches a %s at x=%.0f y=%.0f (half %.0f)\n", what, tp.x, tp.y, tp.half);
                    if (hits == 0 && std::getenv("RG_DEBUG")) {
                        for (auto& ob : objs) {
                            if (std::abs(ob.x - tp.x) > 70 || ob.id > 3000 || ob.kv.count(121) || ob.y > 400) continue;
                            std::printf("      obj %d at (%.1f, %.1f)", ob.id, ob.x, ob.y);
                            for (auto& [kk, vv] : ob.kv) if (kk == 4 || kk == 5 || kk == 6 || kk == 129) std::printf(" k%d=%g", kk, vv);
                            std::printf("\n");
                        }
                        for (auto& q : r.trace)
                            if (std::abs(q.x - tp.x) < 60) std::printf("      path (%.0f, %.1f)\n", q.x, q.y);
                    }
                    hits++;
                }
            }
            std::printf("  playthrough: %zu path points checked, %d touch something\n", r.trace.size(), hits);
            if (r.trace.size() < 200) { std::printf("FAIL: no intended path exported\n"); fails++; }
            if (hits) { std::printf("FAIL: the intended path runs into objects\n"); fails++; }
            // portals must not pile up: a teleport never shares its spot with a speed column
            int piles = 0;
            for (auto& tp : objs) {
                if (tp.id != 747) continue;
                for (auto& sp : objs)
                    if ((sp.id == 200 || sp.id == 201 || sp.id == 202 || sp.id == 203 || sp.id == 1334) && std::abs(sp.x - tp.x) < 40) piles++;
            }
            if (piles) { std::printf("FAIL: %d speed portals sit on a teleport\n", piles); fails++; }
        }
        // move-trigger offsets per group must cancel out before the player passes the group
        // (afterwards things may collapse away behind the player)
        std::map<int, double> groupEnd;
        for (auto& ob : objs) {
            if (ob.id == 901 || !ob.kv.count(57)) continue;
            int g = int(ob.kv[57]);
            groupEnd[g] = std::max(groupEnd.count(g) ? groupEnd[g] : -1e9, ob.x);
        }
        std::map<int, std::pair<double, double>> net;
        for (auto& ob : objs) {
            if (ob.id != 901) continue;
            int g = int(ob.kv[51]);
            if (groupEnd.count(g) && ob.x > groupEnd[g] + 15) continue;
            net[g].first += ob.kv[28];
            net[g].second += ob.kv[29];
        }
        int nz = 0;
        for (auto& [g, v] : net) {
            if (std::abs(v.first) > 1e-6 || std::abs(v.second) > 1e-6) {
                nz++;
                std::printf("  group %d net (%.1f, %.1f), last object x=%.0f\n", g, v.first, v.second,
                            groupEnd.count(g) ? groupEnd[g] : -1.0);
                for (auto& ob : objs)
                    if (ob.id == 901 && int(ob.kv[51]) == g)
                        std::printf("    move at x=%.0f dx=%.0f dy=%.0f\n", ob.x, ob.kv[28], ob.kv[29]);
            }
        }
        if (nz) { std::printf("FAIL: %d groups with non-zero net move\n", nz); fails++; }
    }
    // two-pass generation: pass 1 speed plan, a fake "GD" table whose speed changes happen
    // 16 units later than the model, pass 2 on that table
    {
        GenOptions o;
        o.difficulty = Difficulty::Insane;
        o.seed = 4242;
        o.speedPortalsOnly = true;
        auto p1 = generate(a, o);
        std::vector<double> gd = p1.modelX;
        // shift: x_gd(t) = x_model(t - 16 / v(t)) approximated by delaying every slope change
        for (size_t i = gd.size() - 1; i > 0; i--) {
            double v = (p1.modelX[i] - p1.modelX[i - 1]) / o.timelineStep;
            size_t back = size_t(std::lround(16.0 / std::max(1.0, v) / o.timelineStep));
            if (i > back) gd[i] = p1.modelX[i - back] + 16.0 * 0; // same curve, delayed
        }
        o.speedPortalsOnly = false;
        o.timelineX = gd;
        auto p2 = generate(a, o);
        auto objs2 = parse(p2.objects);
        int speedPortals = 0;
        for (auto& ob : objs2) if (ob.id == 200 || ob.id == 201 || ob.id == 202 || ob.id == 203 || ob.id == 1334) speedPortals++;
        std::printf("\n[two-pass] pass1 %d objects (speed portals), pass2 %d objects, speed portals in pass2: %d\n",
                    p1.objectCount, p2.objectCount, speedPortals);
        if (p2.objectCount < 500 || speedPortals != 0) { std::printf("FAIL: two-pass generation\n"); fails++; }
    }
    if (argc > 2) {
        GenOptions o;
        o.difficulty = Difficulty(argc > 3 ? std::atoi(argv[3]) : 2);
        o.seed = argc > 4 ? uint32_t(std::atoi(argv[4])) : 1;
        o.styleMask = argc > 5 ? uint32_t(std::atoi(argv[5])) : 0;
        auto r = generate(a, o);
        std::printf("\n[file] %s\n", r.summary.c_str());
        std::ofstream(argv[2]) << r.objects;
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
