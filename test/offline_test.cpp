// Offline test for the analyzer + generator (no GD needed).
// Builds a synthetic EDM-like track with known tempo and structure, or loads a 16-bit WAV.
//   offline_test                -> synthetic 128 BPM track
//   offline_test song.wav       -> real song (16-bit PCM wav)

#include "../src/Analysis.hpp"
#include "../src/Generator.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
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
        o.seed = 12345 + d;
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
        // corridor sanity: free space between scaled wall blocks in the same column
        {
            std::map<long, std::pair<double, double>> col; // x -> (bottom wall top, top wall bottom)
            for (auto& ob : objs) {
                if (ob.id != 1 || !ob.kv.count(129)) continue;
                double h = 30 * ob.kv[129];
                long x = std::lround(ob.x);
                auto& c = col.emplace(x, std::make_pair(0.0, 400.0)).first->second;
                if (ob.y - h / 2 < 1) c.first = std::max(c.first, ob.y + h / 2);
                else c.second = std::min(c.second, ob.y - h / 2);
            }
            double minFree = 1e9;
            int run = 0, worstRun = 0;
            long worstX = 0;
            long prevX = -1000;
            for (auto& [x, c] : col) {
                minFree = std::min(minFree, c.second - c.first);
                bool tunnel = c.second < 70 && c.first < 1; // squeezed against the floor
                run = tunnel && x - prevX <= 30 ? run + 1 : tunnel ? 1 : 0;
                if (run > worstRun) worstRun = run, worstX = x;
                prevX = x;
            }
            std::printf("  corridors: min free %.0f units, longest floor tunnel %d columns\n", col.empty() ? 0.0 : minFree, worstRun);
            if (!col.empty() && minFree < 34) { std::printf("FAIL: corridor too tight\n"); fails++; }
            if (worstRun > 12) { std::printf("FAIL: wave/ship stuck in a floor tunnel\n"); fails++; }
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
    if (argc > 2) {
        GenOptions o;
        o.difficulty = Difficulty(argc > 3 ? std::atoi(argv[3]) : 2);
        o.seed = argc > 4 ? uint32_t(std::atoi(argv[4])) : 1;
        std::ofstream(argv[2]) << generate(a, o).objects;
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
