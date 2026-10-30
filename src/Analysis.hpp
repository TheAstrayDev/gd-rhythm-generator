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
    float mid;       // 0..1 mid-band (snare / vocals / leads) strength
    float sustain;   // seconds the note keeps ringing (long notes -> hold mechanics)
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
    std::vector<float> beatKick;      // 0..1 low-band hit on each beat
    std::vector<float> beatSnare;     // 0..1 mid-band hit on each beat
    std::vector<float> beatHat;       // 0..1 high-band hit on each beat

    std::vector<Onset> onsets;        // percussive onsets (drums): the beat pool

    // vocal / melody pool (harmonic part in the voice band), kept apart from the beat
    struct Phrase { double start, end; };
    std::vector<Onset> vocalOnsets;   // syllables / melody note starts (sustain = held length)
    std::vector<float> beatVocal;     // 0..1 voice / melody presence on each beat
    std::vector<Phrase> vocalPhrases; // runs of beats with clear voice / melody
    Onset const* vocalNear(double t, double window = 0.06) const;
    std::vector<Section> sections;

    // Onset strength (0..1) at an arbitrary time, max within +-window seconds.
    float onsetAt(double t, double window = 0.05) const;

    // Index of the beat closest to t (or -1 if none).
    int nearestBeat(double t) const;

    // Strongest onset within +-window of t (nullptr if none).
    Onset const* onsetNear(double t, double window = 0.05) const;

    std::string summary() const;

    // Internal: normalized onset envelope sampled at `envRate` Hz.
    std::vector<float> envelope;
    double envRate = 0;
    double envOffset = 0;
};

// samples: mono float PCM in [-1, 1].
Analysis analyze(std::vector<float> const& samples, int sampleRate);

} // namespace rg
