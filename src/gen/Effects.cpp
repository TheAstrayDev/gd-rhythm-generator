#include "Common.hpp"

// ---------------------------------------------------------------------------------------
// Effects director: maps the song structure and individual hits to triggers.
//
//   song   : intro fade-in, colour scheme (analogous / complementary / triad / split / mono),
//            final fade-out
//   part   : palette per energy level, style-specific shader set, deco skyline (alpha in/out)
//   build  : rising zoom + radial blur, 8th-note pulses in the last bar
//   drop   : shockwave, invert flash (biggest drop), shake, zoom punch, camera tilt swing
//   peak   : chromatic aberration on snares, hue drift, bulge on downbeats (Insane+)
//   calm   : grayscale / sepia after a peak
//   hits   : kick -> background + block pulse, snare -> line + accent pulse
//   change : glitch / pixelate burst on every gamemode transition
//   motion : obstacle fly-in (net-zero move pairs), corridor breathing, deco spin / bounce,
//            alpha cleanup of passed obstacles
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
struct RGB { int r, g, b; };
RGB hsv(double h, double s, double v) {
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360;
    s = std::clamp(s, 0.0, 1.0), v = std::clamp(v, 0.0, 1.0);
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
} // namespace

void Gen::effects() {
    double P = beatLen();
    const double laneY = 600;
    int dif = diff();

    // ---- colour scheme -----------------------------------------------------------------
    double baseHue = rnd() * 360;
    int scheme = rndInt(5);
    static constexpr double offsets[5][3] = {
        {0, 30, 60},     // analogous
        {0, 180, 30},    // complementary
        {0, 120, 240},   // triad
        {0, 150, 210},   // split complementary
        {0, 0, 20},      // monochrome
    };
    static char const* schemeNames[5] = {"palette analogous", "palette complementary", "palette triad",
                                         "palette split", "palette mono"};
    used(schemeNames[scheme]);
    auto hueFor = [&](size_t i, int role) {
        int lv = segs[i].level;
        return baseHue + offsets[scheme][role % 3] + lv * 25 + double(i) * 11;
    };

    auto colorTrig = [&](double x, int channel, RGB c, double dur, double opacity = 1) {
        out.add(obj::TrigColor, x, laneY).kv(key::Red, c.r).kv(key::Green, c.g).kv(key::Blue, c.b)
            .kv(key::Duration, dur).kv(key::TargetColor, channel).kv(key::Opacity, opacity);
    };
    auto pulse = [&](double x, double y, int channel, RGB c, double in, double hold, double fade) {
        out.add(obj::TrigPulse, x, y).kv(key::Red, c.r).kv(key::Green, c.g).kv(key::Blue, c.b)
            .kv(key::FadeIn, in).kv(key::Hold, hold).kv(key::FadeOut, fade)
            .kv(key::PulseMode, 0).kv(key::PulseTargetType, 0).kv(key::TargetGroup, channel);
    };
    auto palette = [&](size_t i, double x, double dur) {
        int lv = segs[i].level;
        colorTrig(x, ch::BG, hsv(hueFor(i, 0), 0.55 + 0.1 * lv, 0.14 + 0.09 * lv), dur);
        colorTrig(x, ch::Ground, hsv(hueFor(i, 0), 0.6 + 0.1 * lv, 0.08 + 0.05 * lv), dur);
        colorTrig(x, ch::Ground2, hsv(hueFor(i, 1), 0.6, 0.12 + 0.05 * lv), dur);
        colorTrig(x, ch::Middle, hsv(hueFor(i, 1), 0.5, 0.2 + 0.05 * lv), dur);
        colorTrig(x, ch::Blocks, hsv(hueFor(i, 1), 0.45 + 0.1 * lv, 0.16 + 0.06 * lv), dur);
        colorTrig(x, ch::Accent, hsv(hueFor(i, 2), 0.35 + 0.1 * lv, 0.85 + 0.05 * lv), dur);
        colorTrig(x, ch::Deco, hsv(hueFor(i, 2), 0.5, 0.55 + 0.08 * lv), dur, 0.45);
        colorTrig(x, ch::DecoGlow, hsv(hueFor(i, 0) + 20, 0.4, 0.9), dur, 0.6);
    };
    // shader helpers
    auto shader = [&](int id, double x, double y) -> Out& { return out.add(id, x, y); };

    // ---- song intro / outro ----------------------------------------------------------
    colorTrig(1, ch::BG, {0, 0, 0}, 0);
    colorTrig(1, ch::Ground, {0, 0, 0}, 0);
    colorTrig(1, ch::Line, {255, 255, 255}, 0);
    out.add(obj::ShaderOptions, 1, laneY + 270);
    palette(0, 3, std::min(4 * P, segs[0].t1 - segs[0].t0));
    used("intro fade-in");
    double endT = segs.back().t1;
    colorTrig(tl.x(std::max(0.0, endT - 4 * P)), ch::BG, {0, 0, 0}, 4 * P);
    colorTrig(tl.x(std::max(0.0, endT - 4 * P)), ch::Ground, {0, 0, 0}, 4 * P);
    used("outro fade-out");

    // biggest drop gets the strongest treatment
    size_t biggest = 0;
    float bestE = -1;
    for (size_t i = 0; i < segs.size(); i++) {
        if (!segs[i].drop) continue;
        float e = A.onsetAt(mt(segs[i].t0), 0.1) + float(segs[i].level);
        if (e > bestE) bestE = e, biggest = i;
    }

    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        double x0 = tl.x(s.t0);
        double v = SPEED_UPS[s.speedRank];
        int lv = s.level;

        if (i > 0) palette(i, x0, s.drop ? 0 : P);

        // ---- gamemode change burst
        if (i > 0 && s.mode != segs[i - 1].mode) {
            if ((s.style + int(i)) % 2 == 0) {
                shader(obj::ShaderGlitch, x0, laneY + 270).kv(key::Duration, 0).kv(key::ShSpeed, 1.2)
                    .kv(key::ShStrength, 1.5).kv(key::ShWidth, 6);
                shader(obj::ShaderGlitch, x0 + v * 0.25, laneY + 270).kv(key::Duration, 0.15).kv(key::ShStrength, 0);
                used("glitch transition");
            } else {
                shader(obj::ShaderPixelate, x0, laneY + 270).kv(key::Duration, 0).kv(key::ShThickness, 6).kv(key::ShTargetY, 6);
                shader(obj::ShaderPixelate, x0 + v * 0.05, laneY + 270).kv(key::Duration, 0.3).kv(key::ShThickness, 0).kv(key::ShTargetY, 0);
                used("pixelate transition");
            }
        }

        // ---- drop
        if (s.drop) {
            out.add(obj::TrigShake, x0, laneY + 150).kv(key::ShakeStrength, 1 + dif / 2)
                .kv(key::ShakeInterval, 0.02).kv(key::Duration, 0.45);
            shader(obj::ShaderShockwave, x0, laneY + 300).kv(key::PlayerCenter, 1).kv(key::Duration, 0.8)
                .kv(key::ShSpeed, 1.4).kv(key::ShStrength, 2.5).kv(key::ShWidth, 30).kv(key::ShThickness, 12);
            out.add(obj::TrigZoom, x0, laneY + 330).kv(key::Zoom, 1.15).kv(key::Duration, 0);
            out.add(obj::TrigZoom, x0 + v * 0.02, laneY + 330).kv(key::Zoom, 1.0).kv(key::Duration, P * 1.5)
                .kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
            used("drop shockwave");
            used("drop zoom punch");
            pulse(x0, laneY + 60, ch::BG, {255, 255, 255}, 0, 0.03, P * 0.8);
            if (i == biggest) {
                shader(obj::ShaderInvert, x0, laneY + 360).kv(key::Duration, 0).kv(key::ShStrength, 1);
                shader(obj::ShaderInvert, x0 + v * 0.05, laneY + 360).kv(key::Duration, P * 0.5).kv(key::ShStrength, 0);
                used("invert flash");
            }
        }

        // ---- build-up into the next drop
        if (s.build) {
            double tb = std::max(s.t0, s.t1 - 8 * P);
            out.add(obj::TrigZoom, tl.x(tb), laneY + 330).kv(key::Zoom, 1.1).kv(key::Duration, s.t1 - tb)
                .kv(key::Easing, ease::In);
            double tr = std::max(s.t0, s.t1 - 4 * P);
            shader(obj::ShaderRadialBlur, tl.x(tr), laneY + 390).kv(key::PlayerCenter, 1)
                .kv(key::Duration, s.t1 - tr).kv(key::ShStrength, 0.6).kv(key::ShWidth, 0.4).kv(key::Easing, ease::In);
            shader(obj::ShaderRadialBlur, tl.x(s.t1), laneY + 390).kv(key::Duration, 0.1).kv(key::ShStrength, 0);
            // snare roll: pulses on every eighth in the last bar
            for (double t = std::max(s.t0, s.t1 - 4 * P); t < s.t1 - 0.01; t += P / 2) {
                pulse(tl.x(t), laneY + 90, ch::Line, {255, 255, 255}, 0, 0, P * 0.3);
            }
            used("build-up zoom + radial blur");
        }

        // ---- calm part after a peak: drain the colour
        if (i > 0 && lv == 0 && segs[i - 1].level >= 2) {
            int id = s.style % 2 ? obj::ShaderSepia : obj::ShaderGray;
            shader(id, x0, laneY + 420).kv(key::Duration, 4 * P).kv(key::ShStrength, 0.75);
            shader(id, tl.x(s.t1), laneY + 420).kv(key::Duration, P).kv(key::ShStrength, 0);
            used(s.style % 2 ? "sepia breakdown" : "grayscale breakdown");
        }

        // ---- peak: hue drift + chromatic aberration on snares
        if (lv >= 3) {
            shader(obj::ShaderHue, x0, laneY + 450).kv(key::Duration, s.t1 - s.t0).kv(key::ShStrength, 40 + rnd() * 60);
            shader(obj::ShaderHue, tl.x(s.t1), laneY + 450).kv(key::Duration, P).kv(key::ShStrength, 0);
            used("hue drift");
        }

        // ---- per-beat hits
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 || t >= s.t1) continue;
            double px = tl.x(t);
            float kick = kickAt(t), snare = snareAt(t);
            bool isDown = down[k];
            if (lv == 0 && !isDown) continue;
            if (lv >= 1 && (kick > 0.45f || isDown)) {
                RGB c = hsv(hueFor(i, 0), 0.45, 0.32 + 0.14 * lv + (isDown ? 0.15 : 0));
                pulse(px, laneY + 30, ch::BG, c, 0, 0.02, P * (isDown ? 0.75 : 0.45));
                if (lv >= 2) pulse(px, laneY + 30, ch::Blocks, hsv(hueFor(i, 1), 0.5, 0.55), 0, 0, P * 0.4);
            }
            if (lv >= 1 && snare > 0.5f && !isDown) {
                pulse(px, laneY + 60, ch::Line, {255, 255, 255}, 0, 0, P * 0.5);
                pulse(px, laneY + 60, ch::Accent, {255, 255, 255}, 0, 0, P * 0.35);
                if (lv >= 3 && dif >= 2) {
                    shader(obj::ShaderChromatic, px, laneY + 480).kv(key::Duration, 0).kv(key::ShThickness, 5)
                        .kv(key::ShTargetY, 0).kv(key::ShUseX, 1);
                    shader(obj::ShaderChromatic, px + v * 0.04, laneY + 480).kv(key::Duration, P * 0.4)
                        .kv(key::ShThickness, 0).kv(key::ShUseX, 1);
                    used("chromatic snare");
                }
            }
            if (lv >= 3 && isDown && dif >= 4) {
                shader(obj::ShaderBulge, px, laneY + 510).kv(key::PlayerCenter, 1).kv(key::Duration, 0)
                    .kv(key::ShStrength, 0.25).kv(key::ShThickness, 300);
                shader(obj::ShaderBulge, px + v * 0.03, laneY + 510).kv(key::Duration, P * 0.5).kv(key::ShStrength, 0);
                used("bulge downbeat");
            }
        }

        // ---- camera tilt swing on peaks
        if (lv >= 3 && dif >= 2) {
            int sign = rnd() < 0.5 ? 1 : -1;
            for (double t = s.t0; t < s.t1 - 2 * P; t += 8 * P) {
                out.add(obj::TrigCamRotate, tl.x(t), laneY + 180).kv(key::Degrees, (2 + rndInt(3)) * sign)
                    .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
                sign = -sign;
            }
            out.add(obj::TrigCamRotate, tl.x(s.t1 - P), laneY + 180).kv(key::Degrees, 0)
                .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
            used("camera tilt");
        }

        // ---- deco skyline (non-solid, above the play area) for open gamemodes
        if (s.mode == Mode::Cube || s.mode == Mode::Ball || s.mode == Mode::Spider) {
            int gDeco = nextGroup++, gGlow = nextGroup++;
            double xa = x0 + 60, xb = tl.x(s.t1) - 30;
            int pattern = (s.style + lv) % 3;
            int n = 0;
            for (double x = xa; x < xb; x += 90, n++) {
                double y = 270 + (pattern == 0 ? 0 : pattern == 1 ? 20 * std::sin(n * 0.7) : (n % 2) * 30);
                out.add(obj::Block, x, y).kv(key::Rotation, 45).kv(key::ScaleX, 0.5).kv(key::ScaleY, 0.5)
                    .kv(key::NoTouch, 1).kv(key::Color1, ch::Deco).kv(key::Color2, ch::Deco).group(gDeco);
                if (n % 3 == 0) {
                    out.add(obj::Block, x + 45, y + 40).kv(key::ScaleX, 0.3).kv(key::ScaleY, 0.3)
                        .kv(key::NoTouch, 1).kv(key::Color1, ch::DecoGlow).kv(key::Color2, ch::DecoGlow).group(gGlow);
                }
            }
            // fade in / out with the part
            out.add(obj::TrigAlpha, std::max(1.0, x0 - 30), laneY + 540).kv(key::TargetGroup, gDeco).kv(key::Duration, 0).kv(key::Opacity, 0);
            out.add(obj::TrigAlpha, x0 + 30, laneY + 540).kv(key::TargetGroup, gDeco).kv(key::Duration, 2 * P).kv(key::Opacity, 1);
            out.add(obj::TrigAlpha, xb - v * P, laneY + 540).kv(key::TargetGroup, gDeco).kv(key::Duration, P).kv(key::Opacity, 0);
            // spin a quarter turn on every beat (each object around its own centre), bounce on kicks
            for (size_t k = 0; k < beats.size(); k++) {
                double t = beats[k];
                if (t < s.t0 + P || t >= s.t1 - P) continue;
                if (lv == 0 && !down[k]) continue;
                double px = tl.x(t);
                out.add(obj::TrigRotate, px, laneY + 570).kv(key::TargetGroup, gDeco).kv(key::Degrees, 90)
                    .kv(key::Duration, P * 0.5).kv(key::Easing, ease::BackOut).kv(key::EasingRate, 1.5);
                if (kickAt(t) > 0.5f && lv >= 1) {
                    out.add(obj::TrigMove, px, laneY + 600).kv(key::TargetGroup, gGlow).kv(key::MoveY, 6).kv(key::Duration, 0.05);
                    out.add(obj::TrigMove, px + v * 0.06, laneY + 600).kv(key::TargetGroup, gGlow).kv(key::MoveY, -6)
                        .kv(key::Duration, P * 0.6).kv(key::Easing, ease::BounceOut);
                }
            }
            used("deco skyline");
        }
    }

    // ---- fly-in obstacles: net-zero move pairs, timed to a beat ------------------------
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
        static constexpr int easings[] = {ease::BackOut, ease::BounceOut, ease::ElasticOut, ease::ExpOut};
        int easing = easings[rndInt(4)];
        double dist = flyDist * (0.6 + rnd() * 0.8);
        out.add(obj::TrigMove, bx - 60, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, 0).kv(key::MoveY, -c.dir * dist).kv(key::Duration, 0);
        out.add(obj::TrigMove, bx, laneY + 90).kv(key::TargetGroup, c.group)
            .kv(key::MoveX, 0).kv(key::MoveY, c.dir * dist).kv(key::Duration, dur)
            .kv(key::Easing, easing).kv(key::EasingRate, 2);
        out.add(obj::TrigAlpha, c.xMax + 120, laneY + 120).kv(key::TargetGroup, c.group)
            .kv(key::Duration, P * 0.5).kv(key::Opacity, 0);
        used("obstacle fly-in");
    }

    // ---- breathing corridor walls (open on the beat, never narrower than base) ----------
    for (auto& w : walls) {
        auto& s = segs[w.seg];
        if (s.level < 2) continue;
        double v = SPEED_UPS[s.speedRank];
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 + P || t >= s.t1 - P) continue;
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
