#include "Generator.hpp"

#include "gen/Common.hpp"

namespace rg {

char const* difficultyName(Difficulty d) {
    static char const* names[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Demon"};
    return names[int(d)];
}

char const* modeName(Mode m) {
    static char const* names[] = {"Cube", "Ship", "Ball", "Wave", "Spider", "Robot", "UFO", "Swing"};
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
    // design layer: every tileset's tiles and slopes, the fill, glow (and the chains / gears older
    // versions placed, so regenerating removes them too)
    for (auto& t : TILESETS)
        for (int id : {t.top, t.outer, t.inner, t.pillar, t.cap, t.slope}) ids.push_back(id);
    for (int id : {FILL_BLOCK, GLOW_LARGE, GLOW_MEDIUM, 1012, 504, 41, 110, 107, 85, 86, 87, 1, 92, 96, 95, 213, 212})
        ids.push_back(id);
    // newer gameplay objects: robot / ufo / swing / dual portals, teleports, blue / black / spider
    // orbs and pads, saws and blades
    for (int id : {obj::PortalRobot, obj::PortalUfo, obj::PortalSwing, obj::PortalDual, obj::PortalDualOff, obj::Teleport,
                   obj::OrbBlue, obj::PadBlue, obj::OrbBlack, obj::OrbSpider, obj::PadSpider})
        ids.push_back(id);
    for (auto& [id, r] : SAWS) ids.push_back(id);
    for (auto& [id, r] : BLADES) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

GenResult generate(Analysis const& a, GenOptions const& opt) {
    gen::Gen g(a, opt);
    return g.run();
}

} // namespace rg
