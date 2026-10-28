#include "Analysis.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numeric>

namespace rg {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr int FRAME = 1024;
constexpr int HOP = 512;

void fft(std::vector<std::complex<float>>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * PI / double(len);
        std::complex<float> wl(float(std::cos(ang)), float(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < len / 2; k++) {
                auto u = a[i + k];
                auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

float percentile(std::vector<float> v, double p) {
    if (v.empty()) return 0;
    size_t k = std::min(v.size() - 1, size_t(p * double(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

void normalizeRange(std::vector<float>& v, double lo = 0.05, double hi = 0.95) {
    float a = percentile(v, lo), b = percentile(v, hi);
    float d = std::max(1e-6f, b - a);
    for (auto& x : v) x = std::clamp((x - a) / d, 0.f, 1.f);
}

// Linear interpolation of an envelope at fractional frame position.
float sampleEnv(std::vector<float> const& e, double pos) {
    if (pos < 0 || pos >= double(e.size() - 1)) return 0;
    size_t i = size_t(pos);
    double f = pos - double(i);
    return float(e[i] * (1 - f) + e[i + 1] * f);
}

} // namespace

float Analysis::onsetAt(double t, double window) const {
    if (envelope.empty()) return 0;
    long a = long(std::floor((t - window - envOffset) * envRate));
    long b = long(std::ceil((t + window - envOffset) * envRate));
    a = std::max(0L, a);
    b = std::min(long(envelope.size()) - 1, b);
    float m = 0;
    for (long i = a; i <= b; i++) m = std::max(m, envelope[size_t(i)]);
    return m;
}

int Analysis::nearestBeat(double t) const {
    if (beats.empty()) return -1;
    auto it = std::lower_bound(beats.begin(), beats.end(), t);
    if (it == beats.end()) return int(beats.size()) - 1;
    int i = int(it - beats.begin());
    if (i > 0 && t - beats[size_t(i - 1)] < *it - t) return i - 1;
    return i;
}

Onset const* Analysis::onsetNear(double t, double window) const {
    auto it = std::lower_bound(onsets.begin(), onsets.end(), t - window,
                               [](Onset const& o, double v) { return o.time < v; });
    Onset const* best = nullptr;
    for (; it != onsets.end() && it->time <= t + window; ++it) {
        if (!best || it->strength > best->strength) best = &*it;
    }
    return best;
}

std::string Analysis::summary() const {
    int counts[4] = {0, 0, 0, 0};
    for (auto& s : sections) counts[std::clamp(s.level, 0, 3)]++;
    char buf[256];
    std::snprintf(
        buf, sizeof(buf), "%.1f BPM%s, %zu beats, %zu onsets, sections: %d calm / %d mid / %d high / %d peak",
        bpm, constantTempo ? "" : " (variable)", beats.size(), onsets.size(),
        counts[0], counts[1], counts[2], counts[3]
    );
    return buf;
}

Analysis analyze(std::vector<float> const& input, int inputRate) {
    Analysis res;
    if (input.empty() || inputRate <= 0) return res;

    // ---- 1. downsample to ~22 kHz mono -------------------------------------------------
    int decim = std::max(1, int(std::lround(double(inputRate) / 22050.0)));
    double sr = double(inputRate) / decim;
    std::vector<float> x(input.size() / size_t(decim));
    for (size_t i = 0; i < x.size(); i++) {
        float s = 0;
        for (int k = 0; k < decim; k++) s += input[i * size_t(decim) + size_t(k)];
        x[i] = s / float(decim);
    }
    res.duration = double(x.size()) / sr;
    if (x.size() < size_t(FRAME * 8)) return res;

    // ---- 2. STFT -> spectral flux per band + RMS ----------------------------------------
    size_t frames = (x.size() - FRAME) / HOP + 1;
    double fps = sr / HOP;
    // time of frame t: the flux peak lines up with the window center (measured offline)
    double frameOffset = double(FRAME) / 2.0 / sr;

    std::vector<float> window(FRAME);
    for (int i = 0; i < FRAME; i++) window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * PI * i / (FRAME - 1)));

    int bins = FRAME / 2;
    double binHz = sr / FRAME;
    int lowEnd = std::max(2, int(200.0 / binHz));
    int midEnd = int(2500.0 / binHz);

    std::vector<float> flux(frames, 0), fluxLow(frames, 0), fluxMid(frames, 0), fluxHigh(frames, 0), rms(frames, 0);
    std::vector<float> prev(size_t(bins), 0), cur(size_t(bins), 0);
    std::vector<std::complex<float>> buf(FRAME);

    for (size_t f = 0; f < frames; f++) {
        size_t off = f * HOP;
        double e = 0;
        for (int i = 0; i < FRAME; i++) {
            float s = x[off + size_t(i)];
            e += double(s) * s;
            buf[size_t(i)] = {s * window[size_t(i)], 0};
        }
        rms[f] = float(std::sqrt(e / FRAME));
        fft(buf);
        float fl = 0, fLow = 0, fMid = 0, fHigh = 0;
        for (int k = 1; k < bins; k++) {
            float mag = std::abs(buf[size_t(k)]) * (2.f / FRAME);
            cur[size_t(k)] = std::log1p(1000.f * mag);
            float d = cur[size_t(k)] - prev[size_t(k)];
            if (d > 0) {
                fl += d;
                if (k < lowEnd) fLow += d;
                else if (k >= midEnd) fHigh += d;
                else fMid += d;
            }
        }
        flux[f] = fl;
        fluxLow[f] = fLow;
        fluxHigh[f] = fHigh;
        fluxMid[f] = fMid;
        std::swap(prev, cur);
    }

    // ---- 3. onset envelope: remove local mean, normalize -----------------------------
    auto onsetEnvelope = [&](std::vector<float> const& src) {
        std::vector<float> e(frames, 0);
        int w = std::max(2, int(0.12 * fps));
        std::vector<double> pre(frames + 1, 0);
        for (size_t i = 0; i < frames; i++) pre[i + 1] = pre[i] + src[i];
        for (size_t i = 0; i < frames; i++) {
            size_t a = i >= size_t(w) ? i - size_t(w) : 0;
            size_t b = std::min(frames, i + size_t(w) + 1);
            double mean = (pre[b] - pre[a]) / double(b - a);
            e[i] = std::max(0.f, float(src[i] - mean));
        }
        double m = 0, s = 0;
        for (auto v : e) m += v;
        m /= double(frames);
        for (auto v : e) s += (v - m) * (v - m);
        s = std::sqrt(s / double(frames));
        if (s < 1e-9) s = 1;
        for (auto& v : e) v = float(v / s);
        return e;
    };
    std::vector<float> odf = onsetEnvelope(flux);
    // Beats are tracked on a kick-weighted envelope: in GD-style music (EDM) the kick sits on
    // the beat while broadband hats/claps often sit on the offbeat.
    std::vector<float> beatEnv(frames);
    {
        auto lowEnv = onsetEnvelope(fluxLow);
        for (size_t i = 0; i < frames; i++) beatEnv[i] = 0.35f * odf[i] + lowEnv[i];
    }

    // ---- 4. tempo: weighted autocorrelation ------------------------------------------
    double period; // in frames
    {
        int lagMin = int(fps * 60.0 / 230.0);
        int lagMax = int(fps * 60.0 / 60.0);
        size_t n = std::min(frames, size_t(fps * 120)); // first 2 minutes is plenty
        size_t start = std::min(frames / 8, size_t(fps * 10));
        if (start + size_t(lagMax) * 4 > frames) start = 0;
        n = std::min(n, frames - start);
        // smooth so that onsets a fraction of a frame apart still correlate
        std::vector<float> acEnv(frames, 0);
        for (size_t i = 2; i + 2 < frames; i++)
            acEnv[i] = (beatEnv[i - 2] + 2 * beatEnv[i - 1] + 3 * beatEnv[i] + 2 * beatEnv[i + 1] + beatEnv[i + 2]) / 9.f;
        auto ac = [&](int lag) {
            if (lag <= 0 || size_t(lag) >= n) return 0.0;
            double s = 0;
            for (size_t i = start; i + size_t(lag) < start + n; i++) s += double(acEnv[i]) * acEnv[i + size_t(lag)];
            return s / double(n - size_t(lag));
        };
        std::vector<double> acv(size_t(lagMax) * 4 + 2, 0);
        for (int l = 1; l < int(acv.size()); l++) acv[size_t(l)] = ac(l);
        auto score = [&](int l) {
            double bpm = 60.0 * fps / l;
            // GD songs skew fast: prefer 140 BPM when the octave is ambiguous
            double oct = std::log2(bpm / 140.0);
            double w = std::exp(-0.5 * (oct / 0.75) * (oct / 0.75));
            // peak height between integer lags (a 14.4-frame period splits over lags 14 and 15)
            double a0 = acv[size_t(l - 1)], a1 = acv[size_t(l)], a2 = acv[size_t(l + 1)];
            double den = a0 - 2 * a1 + a2;
            double s = a1;
            if (den < -1e-12) {
                double dl = std::clamp(0.5 * (a0 - a2) / den, -0.5, 0.5);
                s = std::max(a1, a1 - 0.25 * (a0 - a2) * dl);
            }
            if (4 * l < int(acv.size())) s += 0.15 * acv[size_t(4 * l)];
            return w * s;
        };
        int best = lagMin;
        double bestS = -1;
        for (int l = lagMin; l <= lagMax; l++) {
            double s = score(l);
            if (s > bestS) bestS = s, best = l;
        }
        // parabolic refinement
        double s0 = acv[size_t(best - 1)], s1 = acv[size_t(best)], s2 = acv[size_t(best + 1)];
        double denom = s0 - 2 * s1 + s2;
        double delta = std::abs(denom) > 1e-12 ? 0.5 * (s0 - s2) / denom : 0;
        period = best + std::clamp(delta, -0.5, 0.5);
        double bpm = 60.0 * fps / period;
        while (bpm < 70) bpm *= 2;
        while (bpm > 230) bpm /= 2;
        period = 60.0 * fps / bpm;
    }

    // ---- 5. beat tracking via dynamic programming (Ellis 2007) ------------------------
    std::vector<double> beatFrames;
    {
        const double tight = 300;
        std::vector<double> score(frames, 0);
        std::vector<long> back(frames, -1);
        long pmin = long(std::round(period / 2)), pmax = long(std::round(period * 2));
        for (size_t t = 0; t < frames; t++) {
            double bestV = 0;
            long bestI = -1;
            for (long p = pmin; p <= pmax; p++) {
                long tau = long(t) - p;
                if (tau < 0) break;
                double lr = std::log(double(p) / period);
                double v = score[size_t(tau)] - tight * lr * lr;
                if (bestI < 0 || v > bestV) bestV = v, bestI = tau;
            }
            score[t] = beatEnv[t] + (bestI >= 0 ? std::max(0.0, bestV) : 0);
            back[t] = bestI;
        }
        size_t from = frames > size_t(period) ? frames - size_t(period) : 0;
        size_t endI = from;
        for (size_t t = from; t < frames; t++) if (score[t] > score[endI]) endI = t;
        for (long t = long(endI); t >= 0; t = back[size_t(t)]) {
            beatFrames.push_back(double(t));
            if (back[size_t(t)] >= t) break;
        }
        std::reverse(beatFrames.begin(), beatFrames.end());
    }

    // ---- 6. global constant-tempo grid (most GD songs are steady) ----------------------
    // Search period and phase that best fit the kick-weighted envelope over the WHOLE song,
    // so loud sections with a clear kick outvote intros/breaks without one.
    double beatPeriodSec = period / fps;
    std::vector<double> beats;
    {
        std::vector<float> envS(frames, 0);
        for (size_t i = 1; i + 1 < frames; i++) envS[i] = 0.25f * beatEnv[i - 1] + 0.5f * beatEnv[i] + 0.25f * beatEnv[i + 1];
        auto gridScore = [&](double P, double ph) {
            double s = 0;
            int c = 0;
            for (double f = ph; f < double(frames - 1); f += P, c++) s += sampleEnv(envS, f);
            return c ? s / c : 0.0;
        };
        double bestP = period, bestPh = 0, bestS = -1;
        for (int pi = -150; pi <= 150; pi++) {
            double P = period * (1 + pi * 0.0001);
            for (double ph = 0; ph < P; ph += 0.5) {
                double sc = gridScore(P, ph);
                if (sc > bestS) bestS = sc, bestP = P, bestPh = ph;
            }
        }
        double P1 = bestP, ph1 = bestPh;
        for (int pi = -20; pi <= 20; pi++) {
            double P = P1 * (1 + pi * 0.00001);
            for (double ph = ph1 - 0.6; ph <= ph1 + 0.6; ph += 0.05) {
                double sc = gridScore(P, ph);
                if (sc > bestS) bestS = sc, bestP = P, bestPh = ph;
            }
        }
        // does the DP tracker agree with the grid (on beats or exactly between them)?
        size_t agree = 0;
        for (double f : beatFrames) {
            double d = std::fmod(f - bestPh + bestP * 1000, bestP);
            double e = std::min({d, bestP - d, std::abs(d - bestP / 2)});
            if (e / fps < 0.06) agree++;
        }
        if (beatFrames.empty() || double(agree) / double(beatFrames.size()) >= 0.6) {
            res.constantTempo = true;
            beatPeriodSec = bestP / fps;
            double t0 = bestPh / fps + frameOffset;
            while (t0 - beatPeriodSec > 0) t0 -= beatPeriodSec;
            for (double t = t0; t < res.duration; t += beatPeriodSec) beats.push_back(t);
        }
    }
    if (beats.empty()) {
        for (double f : beatFrames) beats.push_back(f / fps + frameOffset);
        if (beats.size() >= 2) beatPeriodSec = (beats.back() - beats.front()) / double(beats.size() - 1);
    }
    res.beats = beats;
    res.beatPeriod = beatPeriodSec;
    res.bpm = 60.0 / beatPeriodSec;

    // envelope normalized 0..1 for later queries
    {
        std::vector<float> env = odf;
        float hi = percentile(env, 0.98);
        if (hi < 1e-6f) hi = 1;
        for (auto& v : env) v = std::min(1.f, v / hi);
        res.envelope = std::move(env);
        res.envRate = fps;
        res.envOffset = frameOffset;
    }
    auto frameOf = [&](double t) { return std::clamp(long(std::lround((t - frameOffset) * fps)), 0L, long(frames) - 1); };

    // ---- 7. per-beat features + downbeat phase --------------------------------------
    std::vector<float> lowN = fluxLow, midN = fluxMid, highN = fluxHigh, rmsN = rms;
    normalizeRange(lowN, 0.1, 0.98);
    normalizeRange(midN, 0.1, 0.98);
    normalizeRange(highN, 0.1, 0.98);
    normalizeRange(rmsN, 0.05, 0.97);

    size_t nb = beats.size();
    res.beatStrength.resize(nb);
    res.beatEnergy.resize(nb);
    res.beatKick.resize(nb);
    res.beatSnare.resize(nb);
    res.beatHat.resize(nb);
    std::vector<float> beatLow(nb), beatHigh(nb);
    for (size_t i = 0; i < nb; i++) {
        res.beatStrength[i] = res.onsetAt(beats[i], 0.035);
        long f = frameOf(beats[i]);
        float lo = 0;
        for (long k = std::max(0L, f - 2); k <= std::min(long(frames) - 1, f + 2); k++) lo = std::max(lo, lowN[size_t(k)]);
        beatLow[i] = lo;
        float hi = 0;
        for (long k = std::max(0L, f - 2); k <= std::min(long(frames) - 1, f + 2); k++) hi = std::max(hi, highN[size_t(k)]);
        beatHigh[i] = hi;
        float mi = 0;
        for (long k = std::max(0L, f - 2); k <= std::min(long(frames) - 1, f + 2); k++) mi = std::max(mi, midN[size_t(k)]);
        res.beatKick[i] = lo;
        res.beatSnare[i] = mi;
        res.beatHat[i] = hi;
        long f2 = frameOf(beats[i] + beatPeriodSec);
        double e = 0;
        long cnt = 0;
        for (long k = f; k < f2 && k < long(frames); k++, cnt++) e += rmsN[size_t(k)];
        res.beatEnergy[i] = cnt ? float(e / double(cnt)) : 0;
    }
    int phase = 0;
    {
        double best = -1;
        for (int p = 0; p < 4; p++) {
            double s = 0;
            // kick on the downbeat, snare/clap usually on beats 2 and 4
            for (size_t i = size_t(p); i < nb; i += 4) s += beatLow[i] - 0.4 * beatHigh[i];
            for (size_t i = size_t(p) + 1; i < nb; i += 4) s += 0.2 * beatHigh[i];
            // energy changes (drops, breaks) land on bar starts
            for (size_t i = size_t(p) + 4; i < nb; i += 4) s += 3.0 * std::abs(res.beatEnergy[i] - res.beatEnergy[i - 1]);
            if (s > best) best = s, phase = p;
        }
    }
    res.downbeat.assign(nb, false);
    for (size_t i = size_t(phase); i < nb; i += 4) res.downbeat[i] = true;

    // ---- 8. onsets (peak picking) ----------------------------------------------------
    {
        auto& env = res.envelope;
        int w = std::max(2, int(0.05 * fps));
        int wm = std::max(4, int(0.2 * fps));
        size_t minGap = size_t(std::max(1.0, 0.07 * fps));
        size_t last = 0;
        bool any = false;
        for (size_t i = 1; i + 1 < frames; i++) {
            float v = env[i];
            if (v < 0.12f) continue;
            bool peak = true;
            for (int k = -w; k <= w && peak; k++) {
                long j = long(i) + k;
                if (k == 0 || j < 0 || j >= long(frames)) continue;
                if (env[size_t(j)] > v) peak = false;
            }
            if (!peak) continue;
            double mean = 0;
            int c = 0;
            for (int k = -wm; k <= wm; k++) {
                long j = long(i) + k;
                if (j < 0 || j >= long(frames)) continue;
                mean += env[size_t(j)], c++;
            }
            mean /= std::max(1, c);
            if (v < mean + 0.07) continue;
            if (any && i - last < minGap) continue;
            float lo = lowN[i], hi = highN[i];
            float tot = std::max(1e-3f, lo + hi);
            // how long the note rings: loudness stays above half of its peak
            float loud = 0;
            for (size_t k = i; k < std::min(frames, i + 4); k++) loud = std::max(loud, rms[k]);
            size_t e = i;
            while (e + 1 < frames && e - i < size_t(2.0 * fps) && rms[e + 1] > 0.5f * loud) e++;
            res.onsets.push_back({double(i) / fps + frameOffset, v, lo / tot, hi / tot,
                                  midN[i], float(double(e - i) / fps)});
            last = i, any = true;
        }
    }

    // ---- 9. sections from bar-level energy -----------------------------------------
    {
        struct Bar { double start, end; float f; };
        std::vector<Bar> bars;
        for (size_t i = size_t(phase); i + 4 <= nb; i += 4) {
            double s = beats[i], e = i + 4 < nb ? beats[i + 4] : beats[i] + 4 * beatPeriodSec;
            long fa = frameOf(s), fb = frameOf(e);
            double er = 0, el = 0;
            long c = 0;
            for (long k = fa; k < fb; k++, c++) er += rmsN[size_t(k)], el += lowN[size_t(k)];
            int dens = 0;
            for (auto& o : res.onsets) if (o.time >= s && o.time < e) dens++;
            c = std::max(1L, c);
            bars.push_back({s, e, float(0.6 * er / c + 0.25 * el / c + 0.15 * std::min(1.0, dens / 12.0))});
        }
        if (bars.size() >= 2) {
            std::vector<float> f(bars.size());
            for (size_t i = 0; i < bars.size(); i++) f[i] = bars[i].f;
            normalizeRange(f, 0.05, 0.95);
            std::vector<float> sm(f.size());
            for (size_t i = 0; i < f.size(); i++) {
                float a = f[i > 0 ? i - 1 : i], c = f[i + 1 < f.size() ? i + 1 : i];
                sm[i] = 0.25f * a + 0.5f * f[i] + 0.25f * c;
            }
            // phrases of 4 bars
            struct Phrase { double start, end; float e; int bars; };
            std::vector<Phrase> ph;
            for (size_t i = 0; i < bars.size(); i += 4) {
                size_t j = std::min(bars.size(), i + 4);
                float e = 0;
                for (size_t k = i; k < j; k++) e += sm[k];
                ph.push_back({bars[i].start, bars[j - 1].end, e / float(j - i), int(j - i)});
            }
            auto levelOf = [](float e) { return e < 0.3f ? 0 : e < 0.55f ? 1 : e < 0.78f ? 2 : 3; };
            std::vector<Section> secs;
            if (ph.front().start > 0.5) secs.push_back({0, ph.front().start, 0.f, 0, false, 0});
            for (auto& p : ph) {
                int lv = levelOf(p.e);
                if (!secs.empty() && secs.back().level == lv && secs.back().bars > 0 && secs.back().bars + p.bars <= 16) {
                    auto& s = secs.back();
                    s.energy = (s.energy * float(s.bars) + p.e * float(p.bars)) / float(s.bars + p.bars);
                    s.end = p.end;
                    s.bars += p.bars;
                } else {
                    secs.push_back({p.start, p.end, p.e, lv, false, p.bars});
                }
            }
            if (secs.back().end < res.duration) secs.back().end = res.duration;
            for (size_t i = 1; i < secs.size(); i++) {
                int a = secs[i - 1].level, b = secs[i].level;
                secs[i].drop = (b >= 2 && b - a >= 2) || (b == 3 && a < 3) ||
                               (secs[i].energy - secs[i - 1].energy > 0.3f);
            }
            res.sections = std::move(secs);
        }
        if (res.sections.empty()) res.sections.push_back({0, res.duration, 0.5f, 1, false, int(nb / 4)});
    }

    return res;
}

} // namespace rg
