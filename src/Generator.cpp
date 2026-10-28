#include "Generator.hpp"

#include "gen/Common.hpp"

namespace rg {

char const* difficultyName(Difficulty d) {
    static char const* names[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Demon"};
    return names[int(d)];
}

char const* modeName(Mode m) {
    static char const* names[] = {"Cube", "Ship", "Ball", "Wave", "Spider"};
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
    return ids;
}

GenResult generate(Analysis const& a, GenOptions const& opt) {
    gen::Gen g(a, opt);
    return g.run();
}

} // namespace rg
