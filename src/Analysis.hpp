#pragma once

// Pure C++ music analysis (no Geode / GD dependencies), so it can be tested offline.

#include <cstdint>
#include <string>
#include <vector>

namespace rg {

struct Onset {
    double time;     // seconds
    float strength;  // 0..1 (normalized onset envelope peak)
    float low;       // 0..1 share of low-band (kick) flux
    float high;      // 0..1 share of high-band (hats/snare) flux
};

struct Section {
    double start;   // seconds
    double end;     // seconds
    float energy;   // 0..1
    int level;      // 0 calm, 1 medium, 2 high, 3 peak (drop)
    bool drop;      // big energy jump into this section
    int bars;       // length in bars
};

struct Analysis {
    double duration = 0;
    double bpm = 120;
    double beatPeriod = 0.5;
    bool constantTempo = false;

    std::vector<double> beats;        // beat times (seconds)
    std::vector<float> beatStrength;  // 0..1 onset strength on each beat
    std::vector<float> beatEnergy;    // 0..1 loudness during each beat
    std::vector<bool> downbeat;       // true on the first beat of a bar (4/4)

    std::vector<Onset> onsets;
    std::vector<Section> sections;

    // Onset strength (0..1) at an arbitrary time, max within +-window seconds.
    float onsetAt(double t, double window = 0.05) const;

    // Index of the beat closest to t (or -1 if none).
    int nearestBeat(double t) const;

    std::string summary() const;

    // Internal: normalized onset envelope sampled at `envRate` Hz.
    std::vector<float> envelope;
    double envRate = 0;
    double envOffset = 0;
};

// samples: mono float PCM in [-1, 1].
Analysis analyze(std::vector<float> const& samples, int sampleRate);

} // namespace rg
