#include "Common.hpp"

#include <array>

// ---------------------------------------------------------------------------------------
// Effects director. The level's colour palette is never changed (no colour triggers, no
// hue / grayscale / sepia grading, objects keep their colours); hits only flash white. Triggers are used for motion and camera, always in sync
// and always fair:
//
//   entrances : every half-bar chunk of obstacles is hidden off-screen (instant move) and
//               flies into place ON a beat / eighth right in front of the player, finishing
//               at least 0.3 s before the player reaches it. Each part has its own motion
//               theme (rise, drop, slide, swing, stagger) so parts feel different.
//   exits     : what the player has passed collapses down and fades out behind them
//   corridors : walls breathe open on downbeats (never narrower than designed)
//   camera    : zoom out for flying parts, zoom punch + shake on drops, small tilt swings
//               on peaks, shockwave on drops, radial blur rising into drops
//   deco      : translucent non-solid skyline that spins on beats and bounces on kicks
// ---------------------------------------------------------------------------------------

namespace rg::gen {

namespace {
enum Motion { Rise, Drop, Slide, Swing, MOTION_COUNT };
}

void Gen::effects() {
    double P = beatLen();
    const double laneY = 600;
    int dif = diff();

    out.add(obj::ShaderOptions, 1, laneY + 270);

    auto pulse = [&](double x, double y, int channel, std::array<int, 3> c, double in, double hold, double fade) {
        out.add(obj::TrigPulse, x, y).kv(key::Red, c[0]).kv(key::Green, c[1]).kv(key::Blue, c[2])
            .kv(key::FadeIn, in).kv(key::Hold, hold).kv(key::FadeOut, fade)
            .kv(key::PulseMode, 0).kv(key::PulseTargetType, 0).kv(key::TargetGroup, channel);
    };
    // the biggest drop gets the strongest treatment
    size_t biggest = 0;
    float bestE = -1;
    for (size_t i = 0; i < segs.size(); i++) {
        if (!segs[i].drop) continue;
        float e = A.onsetAt(mt(segs[i].t0), 0.1) + float(segs[i].level);
        if (e > bestE) bestE = e, biggest = i;
    }

    // motion theme per part (two styles: one for low objects, one for high objects)
    std::vector<std::pair<Motion, Motion>> theme(segs.size());
    for (size_t i = 0; i < segs.size(); i++) {
        Motion lo = Motion(rndInt(MOTION_COUNT));
        if (lo == Drop) lo = Rise; // ground hazards come from below or ahead
        Motion hi = Motion(rndInt(MOTION_COUNT));
        if (hi == Rise) hi = Drop;
        theme[i] = {lo, hi};
    }

    for (size_t i = 0; i < segs.size(); i++) {
        auto& s = segs[i];
        double x0 = tl.x(s.t0);
        double v = SPEED_UPS[s.speedRank];
        int lv = s.level;
        bool flying = s.mode == Mode::Ship || s.mode == Mode::Wave;

        // ---- camera framing per part
        double baseZoom = flying ? 0.92 : 1.0;
        out.add(obj::TrigZoom, x0, laneY + 330).kv(key::Zoom, baseZoom).kv(key::Duration, P).kv(key::Easing, ease::InOut);
        if (flying) used("zoom out for flying parts");

        // ---- drop
        if (s.drop) {
            out.add(obj::TrigShake, x0, laneY + 150).kv(key::ShakeStrength, 1 + dif / 2)
                .kv(key::ShakeInterval, 0.02).kv(key::Duration, 0.4);
            out.add(obj::ShaderShockwave, x0, laneY + 300).kv(key::PlayerCenter, 1).kv(key::Duration, 0.8)
                .kv(key::ShSpeed, 1.4).kv(key::ShStrength, 2).kv(key::ShWidth, 30).kv(key::ShThickness, 12);
            out.add(obj::TrigZoom, x0 + v * 0.01, laneY + 360).kv(key::Zoom, baseZoom * 1.08).kv(key::Duration, 0);
            out.add(obj::TrigZoom, x0 + v * 0.03, laneY + 360).kv(key::Zoom, baseZoom).kv(key::Duration, P * 1.5)
                .kv(key::Easing, ease::ExpOut).kv(key::EasingRate, 2);
            used("drop shockwave + zoom punch");
        }

        // ---- build-up into the next drop: radial blur rises, released on the drop
        if (s.build) {
            double tr = std::max(s.t0, s.t1 - 4 * P);
            out.add(obj::ShaderRadialBlur, tl.x(tr), laneY + 390).kv(key::PlayerCenter, 1)
                .kv(key::Duration, s.t1 - tr).kv(key::ShStrength, 0.3).kv(key::ShWidth, 0.3).kv(key::Easing, ease::In);
            out.add(obj::ShaderRadialBlur, tl.x(s.t1), laneY + 390).kv(key::Duration, 0.1).kv(key::ShStrength, 0);
            used("build-up radial blur");
        }

        // ---- peaks: tilt swings and heavy downbeats
        if (lv >= 3 && dif >= 2) {
            int sign = rnd() < 0.5 ? 1 : -1;
            for (double t = s.t0; t < s.t1 - 2 * P; t += 8 * P) {
                out.add(obj::TrigCamRotate, tl.x(t), laneY + 180).kv(key::Degrees, 2 * sign)
                    .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
                sign = -sign;
            }
            out.add(obj::TrigCamRotate, tl.x(s.t1 - P), laneY + 180).kv(key::Degrees, 0)
                .kv(key::Duration, P).kv(key::Easing, ease::SineInOut);
            int bar = 0;
            for (size_t k = 0; k < beats.size(); k++) {
                if (!down[k] || beats[k] < s.t0 + P || beats[k] >= s.t1 - P) continue;
                if (bar++ % 2 == 0 && kickAt(beats[k]) > 0.6f) {
                    out.add(obj::TrigShake, tl.x(beats[k]), laneY + 150).kv(key::ShakeStrength, 1)
                        .kv(key::ShakeInterval, 0.02).kv(key::Duration, 0.12);
                }
            }
            used("camera tilt + downbeat shake");
        }

        // ---- gamemode change burst
        if (i > 0 && s.mode != segs[i - 1].mode) {
            if ((s.style + int(i)) % 2 == 0) {
                out.add(obj::ShaderGlitch, x0, laneY + 270).kv(key::Duration, 0).kv(key::ShSpeed, 1.2)
                    .kv(key::ShStrength, 1.5).kv(key::ShWidth, 6);
                out.add(obj::ShaderGlitch, x0 + v * 0.25, laneY + 270).kv(key::Duration, 0.15).kv(key::ShStrength, 0);
                used("glitch transition");
            } else {
                out.add(obj::ShaderPixelate, x0, laneY + 270).kv(key::Duration, 0).kv(key::ShThickness, 6).kv(key::ShTargetY, 6);
                out.add(obj::ShaderPixelate, x0 + v * 0.05, laneY + 270).kv(key::Duration, 0.3).kv(key::ShThickness, 0).kv(key::ShTargetY, 0);
                used("pixelate transition");
            }
        }

        // ---- biggest drop: a quick invert flash (returns to the level's own colours)
        if (s.drop && i == biggest) {
            out.add(obj::ShaderInvert, x0, laneY + 360).kv(key::Duration, 0).kv(key::ShStrength, 1);
            out.add(obj::ShaderInvert, x0 + v * 0.05, laneY + 360).kv(key::Duration, P * 0.5).kv(key::ShStrength, 0);
            used("invert flash");
        }

        // ---- hits: white flashes only (the palette itself never changes)
        for (size_t k = 0; k < beats.size(); k++) {
            double t = beats[k];
            if (t < s.t0 || t >= s.t1) continue;
            double px = tl.x(t);
            float kick = kickAt(t), snare = snareAt(t);
            bool isDown = down[k];
            if (lv == 0 && !isDown) continue;
            if (lv >= 1 && (kick > 0.45f || isDown)) {
                pulse(px, laneY + 30, ch::BG, {255, 255, 255}, 0, 0, P * (isDown ? 0.35 : 0.2));
                used("kick flash");
            }
            if (lv >= 1 && snare > 0.5f && !isDown) {
                pulse(px, laneY + 60, ch::Line, {255, 255, 255}, 0, 0, P * 0.5);
                if (lv >= 3 && dif >= 2) {
                    out.add(obj::ShaderChromatic, px, laneY + 480).kv(key::Duration, 0).kv(key::ShThickness, 5)
                        .kv(key::ShTargetY, 0).kv(key::ShUseX, 1);
                    out.add(obj::ShaderChromatic, px + v * 0.04, laneY + 480).kv(key::Duration, P * 0.4)
                        .kv(key::ShThickness, 0).kv(key::ShUseX, 1);
                    used("chromatic snare");
                }
            }
            if (lv >= 3 && isDown && dif >= 4) {
                out.add(obj::ShaderBulge, px, laneY + 510).kv(key::PlayerCenter, 1).kv(key::Duration, 0)
                    .kv(key::ShStrength, 0.25).kv(key::ShThickness, 300);
                out.add(obj::ShaderBulge, px + v * 0.03, laneY + 510).kv(key::Duration, P * 0.5).kv(key::ShStrength, 0);
                used("bulge downbeat");
            }
        }

        // ---- deco skyline (non-solid, above the play area) for open gamemodes
        if (!flying) {
            int gDeco = nextGroup++, gGlow = nextGroup++;
            double xa = x0 + 60, xb = tl.x(s.t1) - 30;
            int pattern = rndInt(3);
            int n = 0;
            for (double x = xa; x < xb; x += 90, n++) {
                double y = 270 + (pattern == 0 ? 0 : pattern == 1 ? 20 * std::sin(n * 0.7) : (n % 2) * 30);
                out.add(obj::Block, x, y).kv(key::Rotation, 45).kv(key::ScaleX, 0.5).kv(key::ScaleY, 0.5)
                    .kv(key::NoTouch, 1).group(gDeco);
                if (n % 3 == 0) {
                    out.add(obj::Block, x + 45, y + 40).kv(key::ScaleX, 0.3).kv(key::ScaleY, 0.3)
                        .kv(key::NoTouch, 1).group(gGlow);
                }
            }
            for (int g : {gDeco, gGlow}) {
                out.add(obj::TrigAlpha, std::max(1.0, x0 - 30), laneY + 540).kv(key::TargetGroup, g).kv(key::Duration, 0).kv(key::Opacity, 0);
                out.add(obj::TrigAlpha, x0 + 30, laneY + 540).kv(key::TargetGroup, g).kv(key::Duration, 2 * P).kv(key::Opacity, 0.35);
                out.add(obj::TrigAlpha, xb - v * P, laneY + 540).kv(key::TargetGroup, g).kv(key::Duration, P).kv(key::Opacity, 0);
            }
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

    // ---- obstacle entrances: hidden off-screen, arrive on an eighth in front of the player
    std::vector<double> grid; // beats and eighths
    for (size_t k = 0; k < beats.size(); k++) {
        grid.push_back(beats[k]);
        grid.push_back(beats[k] + P / 2);
    }
    std::sort(grid.begin(), grid.end());
    static constexpr char const* names[] = {"entrance rise", "entrance drop", "entrance slide", "entrance swing"};
    for (auto& [k, c] : chunks) {
        size_t si = size_t(k >> 21);
        auto& s = segs[si];
        double v = SPEED_UPS[s.speedRank];
        Motion m = c.dir > 0 ? theme[si].second : theme[si].first;
        double dur = std::min(0.35, P * 0.5);
        // arrive late (it is a show) but finish >= 0.3 s before the player's front edge
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

        // exit: collapse and fade once the player is fully past
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
