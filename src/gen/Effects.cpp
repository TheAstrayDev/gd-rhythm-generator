#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Effects director. Uses the whole trigger / shader toolbox EXCEPT colour grading: the
// level palette and object colours never change (no colour triggers, no hue / grayscale /
// sepia); hits only flash white.
//
// Playability first: every gameplay object the generator placed is on a "heat" map. Before
// a screen-wide effect (shockwave, shockline, bulge, pinch, lens circle, radial / motion
// blur, pixelate, glitch, chromatic glitch, chromatic aberration, invert flash) goes in,
// the director looks at how much the player has to read during the effect and right after
// it: calm -> full strength, busy -> a lighter version, very busy -> nothing. Camera moves
// are toned down the same way.
//
//   song   : intro vignette opening, outro vignette closing
//   part   : framing zoom (flying parts zoom out), transition burst (pixelate / glitch /
//            chromatic glitch, rotating), entrances / exits of obstacles, breathing walls
//   drop   : shockwave or shockline sweep, zoom punch, shake, invert flash (biggest drop)
//   build  : radial blur and zoom rising into the drop, eighth-note line flashes
//   peak   : chromatic aberration on snares, bulge / pinch on downbeats, tilt swings,
//            micro zoom punches
//   calm   : lens circle breathing on downbeats
//   whoosh : a 0.15 s motion blur at the start of a calm-enough drop, then fully off
//   hits   : white background flash on kicks, line flash on snares
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
enum Motion { Rise, Drop, Slide, Swing, MOTION_COUNT };
}

void Gen::effects() {
    double P = beatLen();
    const double laneY = 600;
    int dif = diff();

    // ---- heat map --------------------------------------------------------------------
    auto heat = out.heat;
    std::sort(heat.begin(), heat.end());
    std::vector<double> hx, hw{0};
    for (auto& [x, w] : heat) hx.push_back(x), hw.push_back(hw.back() + w);
    auto heatIn = [&](double x0, double x1) {
        size_t i0 = size_t(std::lower_bound(hx.begin(), hx.end(), x0) - hx.begin());
        size_t i1 = size_t(std::upper_bound(hx.begin(), hx.end(), x1) - hx.begin());
        return hw[i1] - hw[i0];
    };
    // 0 = nothing to read, 1 = dense: what the player faces during [t, t + dur] and 0.6 s after
    auto busy = [&](double t, double dur) {
        double x0 = tl.x(t), x1 = tl.x(t + dur + 0.6);
        double perHundred = heatIn(x0, x1) / std::max(30.0, x1 - x0) * 100;
        return std::clamp(perHundred / 1.6, 0.0, 1.0);
    };
    // strength factor for a screen-wide effect: 1 when calm, lighter when busy, 0 when dense
    auto allow = [&](double t, double dur) {
        double b = busy(t, dur);
        return b < 0.35 ? 1.0 : b < 0.6 ? 0.45 : 0.0;
    };

    auto pulse = [&](double x, double y, int channel, double in, double hold, double fade) {
        out.add(obj::TrigPulse, x, y).kv(key::Red, 255).kv(key::Green, 255).kv(key::Blue, 255)
            .kv(key::FadeIn, in).kv(key::Hold, hold).kv(key::FadeOut, fade)
            .kv(key::PulseMode, 0).kv(key::PulseTargetType, 0).kv(key::TargetGroup, channel);
    };
    // a shader that snaps to a value, then tweens back
    auto shaderPulse = [&](int id, double x, double y, double backAfter, double fadeBack,
                           std::initializer_list<std::pair<int, double>> on,
                           std::initializer_list<std::pair<int, double>> off) {
        auto& o1 = out.add(id, x, y).kv(key::Duration, 0);
        for (auto& [k, val] : on) o1.kv(k, val);
        auto& o2 = out.add(id, x + backAfter, y).kv(key::Duration, fadeBack);
        for (auto& [k, val] : off) o2.kv(k, val);
    };

    out.add(obj::ShaderOptions, 1, laneY + 270);

    // ---- song intro / outro: vignette opens and closes ------------------------------
    {
        out.add(obj::ShaderLensCircle, 1, laneY + 630).kv(key::PlayerCenter, 1).kv(key::Duration, 0)
            .kv(key::ShStrength, 1).kv(key::ShWidth, 120);
        out.add(obj::ShaderLensCircle, 3, laneY + 630).kv(key::PlayerCenter, 1).kv(key::Duration, 4 * P)
            .kv(key::ShStrength, 0).kv(key::ShWidth, 600).kv(key::Easing, ease::SineOut);
        double te = segs.back().t1;
        out.add(obj::ShaderLensCircle, tl.x(te - 3 * P), laneY + 630).kv(key::PlayerCenter, 1)
            .kv(key::Duration, 3 * P).kv(key::ShStrength, 1).kv(key::ShWidth, 120).kv(key::Easing, ease::SineInOut);
        used("vignette intro / outro");
    }

    size_t biggest = 0;
    float bestE = -1;
    for (size_t i = 0; i < segs.size(); i++) {
        if (!segs[i].drop) continue;
        float e = A.onsetAt(mt(segs[i].t0), 0.1) + float(segs[i].level);
        if (e > bestE) bestE = e, biggest = i;
    }

    std::vector<std::pair<Motion, Motion>> theme(segs.size());
    for (size_t i = 0; i < segs.size(); i++) {
        Motion lo = Motion(rndInt(MOTION_COUNT));
        if (lo == Drop) lo = Rise;
        Motion hi = Motion(rndInt(MOTION_COUNT));
        if (hi == Rise) hi = Drop;
        theme[i] = {lo, hi};
    }

    int transitionKind = 0;
    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        double x0 = tl.x(s.t0);
        double v = SPEED_UPS[s.speedRank];
        int lv = s.level;
        bool flying = s.mode == Mode::Ship || s.mode == Mode::Wave;
        double baseZoom = flying ? 0.92 : 1.0;
        out.add(obj::TrigZoom, x0, laneY + 330).kv(key::Zoom, baseZoom).kv(key::Duration, P).kv(key::Easing, ease::InOut);

        // ---- transition burst at the start of a part (gameplay starts a moment later)
        if (i > 0 && s.mode != segs[i - 1].mode) {
            double f = allow(s.t0, 0.3);
            if (f > 0) {
                switch (transitionKind++ % 3) {
                    case 0:
                        shaderPulse(obj::ShaderPixelate, x0, laneY + 270, v * 0.05, 0.3,
                                    {{key::ShThickness, 6 * f}, {key::ShTargetY, 6 * f}}, {{key::ShThickness, 0}, {key::ShTargetY, 0}});
                        used("pixelate transition");
                        break;
                    case 1:
                        shaderPulse(obj::ShaderGlitch, x0, laneY + 270, v * 0.25, 0.15,
                                    {{key::ShSpeed, 1.2}, {key::ShStrength, 1.5 * f}, {key::ShWidth, 6}}, {{key::ShStrength, 0}});
                        used("glitch transition");
                        break;
                    default:
                        shaderPulse(obj::ShaderChromaticGlitch, x0, laneY + 270, v * 0.2, 0.2,
                                    {{key::ShSpeed, 1}, {key::ShStrength, 1 * f}, {key::ShThickness, 4 * f}},
                                    {{key::ShStrength, 0}, {key::ShThickness, 0}});
                        used("chromatic glitch transition");
                        break;
                }
            }
        }

        // ---- drop: the first moments of a part are calm by construction
        if (s.drop) {
            out.add(obj::TrigShake, x0, laneY + 150).kv(key::ShakeStrength, 1 + dif / 2)
                .kv(key::ShakeInterval, 0.02).kv(key::Duration, 0.35);
            double f = allow(s.t0, 0.5);
            if (f > 0) {
                if (i % 2 == 0) {
                    out.add(obj::ShaderShockwave, x0, laneY + 300).kv(key::PlayerCenter, 1).kv(key::Duration, 0.6)
                        .kv(key::ShSpeed, 1.4).kv(key::ShStrength, 2 * f).kv(key::ShWidth, 30).kv(key::ShThickness, 12);
                    used("drop shockwave");
                } else {
                    out.add(obj::ShaderShockline, x0, laneY + 300).kv(key::PlayerCenter, 1).kv(key::Duration, 0.6)
                        .kv(key::ShSpeed, 1.5).kv(key::ShStrength, 2 * f).kv(key::ShWidth, 25).kv(key::ShThickness, 10);
                    used("drop shockline");
                }
                if (i == biggest) {
                    shaderPulse(obj::ShaderInvert, x0, laneY + 360, v * 0.05, P * 0.4, {{key::ShStrength, 1}}, {{key::ShStrength, 0}});
                    used("invert flash");
                }
            }
            out.add(obj::TrigZoom, x0 + v * 0.01, laneY + 360).kv(key::Zoom, baseZoom * 1.06).kv(key::Duration, 0);
            out.add(obj::TrigZoom, x0 + v * 0.03, laneY + 360).kv(key::Zoom, baseZoom).kv(key::Duration, P * 1.5)
                .kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
            pulse(x0, laneY + 30, ch::BG, 0, 0.03, P * 0.6);
            used("drop zoom punch + shake");
            // a very short horizontal "whoosh", fully switched off again after 0.15 s
            if (double wf = allow(s.t0, 0.25); wf > 0) {
                out.add(obj::ShaderMotionBlur, x0, laneY + 660).kv(key::Duration, 0.05).kv(key::ShStrength, 0.35 * wf)
                    .kv(key::ShThickness, 0.3).kv(key::ShUseX, 1).kv(key::ShTargetY, 0).kv(key::ShUseY, 0);
                out.add(obj::ShaderMotionBlur, x0 + v * 0.15, laneY + 660).kv(key::Duration, 0.1).kv(key::ShStrength, 0)
                    .kv(key::ShThickness, 0).kv(key::ShUseX, 0).kv(key::ShTargetY, 0).kv(key::ShUseY, 0);
                used("drop whoosh (motion blur)");
            }
        }

        // ---- build-up: radial blur rising, only as strong as the gameplay allows
        if (s.build) {
            double tb = std::max(s.t0, s.t1 - 4 * P);
            for (double t = tb; t < s.t1 - 0.01; t += P) {
                double u = (t - tb + P) / (s.t1 - tb);
                out.add(obj::ShaderRadialBlur, tl.x(t), laneY + 390).kv(key::PlayerCenter, 1)
                    .kv(key::Duration, P).kv(key::ShStrength, 0.3 * u * allow(t, P)).kv(key::ShWidth, 0.3);
            }
            out.add(obj::ShaderRadialBlur, tl.x(s.t1), laneY + 390).kv(key::Duration, 0.1).kv(key::ShStrength, 0);
            out.add(obj::TrigZoom, tl.x(std::max(s.t0, s.t1 - 8 * P)), laneY + 330).kv(key::Zoom, baseZoom * 1.06)
                .kv(key::Duration, std::min(8 * P, s.t1 - s.t0)).kv(key::Easing, ease::In);
            for (double t = std::max(s.t0, s.t1 - 4 * P); t < s.t1 - 0.01; t += P / 2) pulse(tl.x(t), laneY + 90, ch::Line, 0, 0, P * 0.3);
            used("build-up radial blur + zoom");
        }

        // ---- per-beat effects
        int bar = 0;
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 || t >= s.t1) continue;
            double px = tl.x(t);
            float kick = kickAt(t), snare = snareAt(t);
            bool isDown = down[k];
            double bz = busy(t, 0.3);
            if (isDown) bar++;
            // white flashes (shorter when the player is busy)
            if ((lv >= 1 && (kick > 0.45f || isDown)) || (lv == 0 && isDown)) {
                pulse(px, laneY + 30, ch::BG, 0, 0, P * (isDown ? 0.35 : 0.2) * (bz > 0.7 ? 0.5 : 1.0));
                used("kick flash");
            }
            if (lv >= 1 && snare > 0.5f && !isDown) pulse(px, laneY + 60, ch::Line, 0, 0, P * 0.5);

            if (lv >= 3) {
                if (snare > 0.5f && !isDown && dif >= 2) {
                    double f = allow(t, 0.2);
                    if (f > 0) {
                        shaderPulse(obj::ShaderChromatic, px, laneY + 480, v * 0.04, P * 0.4,
                                    {{key::ShThickness, 4 * f}, {key::ShTargetY, 0}, {key::ShUseX, 1}},
                                    {{key::ShThickness, 0}, {key::ShUseX, 1}});
                        used("chromatic snare");
                    }
                }
                if (isDown && dif >= 3) {
                    if (allow(t, 0.4) >= 1) {
                        if (bar % 2) {
                            shaderPulse(obj::ShaderBulge, px, laneY + 510, v * 0.03, P * 0.5,
                                        {{key::PlayerCenter, 1}, {key::ShStrength, 0.25}, {key::ShThickness, 300}}, {{key::ShStrength, 0}});
                            used("bulge downbeat");
                        } else {
                            shaderPulse(obj::ShaderPinch, px, laneY + 510, v * 0.03, P * 0.5,
                                        {{key::PlayerCenter, 1}, {key::ShStrength, 0.2}, {key::ShThickness, 300}}, {{key::ShStrength, 0}});
                            used("pinch downbeat");
                        }
                    }
                    if (bz < 0.6) {
                        out.add(obj::TrigZoom, px, laneY + 360).kv(key::Zoom, baseZoom * 1.03).kv(key::Duration, 0);
                        out.add(obj::TrigZoom, px + v * 0.02, laneY + 360).kv(key::Zoom, baseZoom).kv(key::Duration, P * 0.8)
                            .kv(key::Easing, ease::ExpOut);
                        used("micro zoom punch");
                    }
                }
            }
            // calm parts: breathing vignette on downbeats, motion blur on long notes
            if (lv == 0 && isDown && allow(t, P) >= 1) {
                shaderPulse(obj::ShaderLensCircle, px, laneY + 630, v * 0.02, P * 1.5,
                            {{key::PlayerCenter, 1}, {key::ShStrength, 0.35}, {key::ShWidth, 260}}, {{key::ShStrength, 0}});
                used("lens breathing");
            }
        }

        // ---- peaks: tilt swings, skipped where the player is busy
        if (lv >= 3 && dif >= 2) {
            int sign = rnd() < 0.5 ? 1 : -1;
            bool tilted = false;
            for (double t = s.t0; t < s.t1 - 2 * P; t += 8 * P) {
                if (busy(t, P) > 0.6) continue;
                out.add(obj::TrigCamRotate, tl.x(t), laneY + 180).kv(key::Degrees, 2 * sign)
                    .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
                sign = -sign;
                tilted = true;
            }
            if (tilted) {
                out.add(obj::TrigCamRotate, tl.x(s.t1 - P), laneY + 180).kv(key::Degrees, 0)
                    .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
                used("camera tilt");
            }
        }
    }

    // ---- obstacle entrances: hidden off-screen, arrive on an eighth in front of the player
    std::vector<double> grid;
    for (size_t k = 0; k < beats.size(); k++) grid.push_back(beats[k]), grid.push_back(beats[k] + P / 2);
    std::sort(grid.begin(), grid.end());
    static constexpr char const* names[] = {"entrance rise", "entrance drop", "entrance slide", "entrance swing"};
    for (auto& [k, c] : chunks) {
        size_t si = size_t(k >> 21);
        auto& s = segs[si];
        double v = SPEED_UPS[s.speedRank];
        Motion m = c.dir > 0 ? theme[si].second : theme[si].first;
        double dur = std::min(0.35, P * 0.5);
        double latest = c.xMin - 30 - v * (dur + 0.3);
        double earliest = latest - v * 0.9;
        double tb = -1;
        for (double gt : grid) {
            double gx = tl.x(gt);
            if (gx <= latest && gx >= earliest) tb = gt;
        }
        if (tb < 0) continue;
        double bx = tl.x(tb);
        double dx = 0, dy = 0;
        int easing = ease::BackOut;
        switch (m) {
            case Rise: dy = -(90 + rnd() * 60), easing = rnd() < 0.5 ? ease::BackOut : ease::ExpOut; break;
            case Drop: dy = 120 + rnd() * 60, easing = ease::BounceOut; break;
            case Slide: dx = 90 + rnd() * 60, easing = ease::ExpOut; break;
            case Swing: dx = 60, dy = c.dir > 0 ? 90 : -90, easing = ease::ElasticOut; break;
            default: break;
        }
        double hideX = std::max(1.0, c.xMin - 950);
        out.add(obj::TrigMove, hideX, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, dx).kv(key::MoveY, dy).kv(key::Duration, 0);
        out.add(obj::TrigMove, bx, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, -dx).kv(key::MoveY, -dy).kv(key::Duration, dur)
            .kv(key::Easing, easing).kv(key::EasingRate, 2);
        used(names[m]);
        double exitX = c.xMax + 90;
        out.add(obj::TrigMove, exitX, laneY + 120).kv(key::TargetGroup, c.group)
            .kv(key::MoveY, -240).kv(key::Duration, 0.6).kv(key::Easing, ease::In);
        out.add(obj::TrigAlpha, exitX, laneY + 120).kv(key::TargetGroup, c.group)
            .kv(key::Duration, 0.4).kv(key::Opacity, 0);
        used("collapse behind");
    }

    // ---- breathing corridor walls (open on the beat, never narrower than base) ----------
    for (auto& w : walls) {
        auto& s = segs[w.seg];
        if (s.level < 2) continue;
        double v = SPEED_UPS[s.speedRank];
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 + P || t >= s.t1 - 4 * P) continue;
            if (!down[k] && s.level < 3) continue;
            double x = tl.x(t);
            out.add(obj::TrigMove, x, laneY + 210).kv(key::TargetGroup, w.top).kv(key::MoveY, 8).kv(key::Duration, 0.05);
            out.add(obj::TrigMove, x + v * 0.06, laneY + 210).kv(key::TargetGroup, w.top).kv(key::MoveY, -8)
                .kv(key::Duration, P * 0.6).kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
            out.add(obj::TrigMove, x, laneY + 240).kv(key::TargetGroup, w.bottom).kv(key::MoveY, -8).kv(key::Duration, 0.05);
            out.add(obj::TrigMove, x + v * 0.06, laneY + 240).kv(key::TargetGroup, w.bottom).kv(key::MoveY, 8)
                .kv(key::Duration, P * 0.6).kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
        }
        used("breathing walls");
    }
}

} // namespace rg::gen
