#include "Generator.hpp"

#include "gen/Common.hpp"

namespace rg {

char const* difficultyName(Difficulty d) {
    static char const* names[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Demon"};
    return names[int(d)];
}

char const* modeName(Mode m) {
    static char const* names[] = {"Cube", "Ship", "Ball", "Wave", "Spider", "Robot"};
    return names[int(m)];
}

std::vector<int> generatedObjectIds() {
    using namespace gen;
    std::vector<int> ids = {
        obj::Block, obj::Spike, obj::PadYellow, obj::PadPink, obj::PadRed,
        obj::OrbYellow, obj::OrbPink, obj::OrbRed, obj::OrbDashGreen,
        obj::PortalGravityDown, obj::PortalGravityUp, obj::PortalCube, obj::PortalShip,
        obj::PortalMirrorOn, obj::PortalMirrorOff, obj::PortalBall, obj::PortalSizeNormal,
        obj::PortalSizeMini, obj::PortalWave, obj::PortalSpider,
        obj::TrigColor, obj::TrigMove, obj::TrigPulse, obj::TrigAlpha, obj::TrigToggle, obj::TrigRotate,
        obj::TrigShake, obj::TrigZoom, obj::TrigCamOffset, obj::TrigCamRotate, obj::TrigScale,
        obj::ShaderOptions, obj::ShaderShockwave, obj::ShaderShockline, obj::ShaderGlitch,
        obj::ShaderChromatic, obj::ShaderChromaticGlitch, obj::ShaderPixelate, obj::ShaderLensCircle,
        obj::ShaderRadialBlur, obj::ShaderMotionBlur, obj::ShaderBulge, obj::ShaderPinch, obj::ShaderGray,
        obj::ShaderSepia, obj::ShaderInvert, obj::ShaderHue,
    };
    for (int id : SPEED_PORTAL) ids.push_back(id);
    // design layer: tiles, glow, chains, gears; robot portal; blue pad / orb
    for (int id : {2, 3, 6, 7, 83, 70, 71, 74, 75, 94, 76, 77, 78, 81, 82, 207, 208, 209, 467, 469, 470, 471, 91,
                   1011, 503, 1012, 504, 41, 110, 107, 85, 86, 87, 745, 67, 84})
        ids.push_back(id);
    return ids;
}

GenResult generate(Analysis const& a, GenOptions const& opt) {
    gen::Gen g(a, opt);
    return g.run();
}

} // namespace rg
