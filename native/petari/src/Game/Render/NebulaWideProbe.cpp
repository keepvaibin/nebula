// GALAXY_TEST_WIDE_TABLES=1 in a development build: a screen-wide draw through the written shader wide_probe.hlsl with a list
// of 24 maps. Map 23 holds a red no other map has; the shader samples it and writes it as the pixel's depth, and a depth
// probe at the middle of the picture brings it back on either back end. One log line says what was read.
#include "Game/Render/NebulaWideProbe.hpp"

#if defined(NEBULA_DEV)
#include "Game/Render/FrameList.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include <nebula_geometry.hpp>
#include <nebula_module.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    const u32 cMaps = 24;
    const u32 cPicked = 23;
    const u32 cChannel = 15;
    const u32 cGiveUpFrames = 600;

    // Map n is a 1 x 1 picture; its red is 10 + 10 n, so map 23 reads 240 and the sixteenth and last of the old table 160.
    u8 sImages[cMaps][4];
    u32 sIds[cMaps];
    bool sBuilt;
    u32 sAsker;
    u32 sFrames;
    s32 sWanted = -1;
    bool sDone;

    u8 redOf(u32 map) {
        return static_cast< u8 >(10 + 10 * map);
    }
}  // namespace

namespace NebulaWideProbe {
    void draw() {
        if (sWanted < 0) {
            sWanted = nebula_host()->option(nebula_host()->user, NEBULA_OPTION_TEST_WIDE_TABLES) != 0 ? 1 : 0;
            sAsker = Frame::newProbeAsker();
        }
        if (sWanted == 0 || sDone) {
            return;
        }
        // What the probe of an earlier frame read at the middle of the picture.
        NebulaScreenProbe answer;
        if (Frame::screenProbeResults(cChannel, sAsker, &answer, 1) == 1 && answer.x >= 0.0f) {
            const u32 read = static_cast< u32 >(answer.value * 255.0f + 0.5f);
            nebula_log("[wide-table] map %u of %u: red read %u, expected %u: %s
", static_cast< unsigned >(cPicked),
                       static_cast< unsigned >(cMaps), static_cast< unsigned >(read), static_cast< unsigned >(redOf(cPicked)),
                       read == redOf(cPicked) ? "ok" : "WRONG");
            sDone = true;
            return;
        }
        if (++sFrames > cGiveUpFrames) {
            nebula_log("[wide-table] no answer after %u frames
", static_cast< unsigned >(cGiveUpFrames));
            sDone = true;
            return;
        }

        SiteMaterial material;
        material.init(static_cast< NebulaShaderId >(NEBULA_SHADER_WIDE_PROBE), SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, true,
                      NEBULA_CULL_NONE);
        // Only the depth is written.
        material.mState.color_write = 0;
        material.mState.alpha_write = 0;
        material.constants[0] = static_cast< f32 >(cPicked);
        SiteTexture sampling;
        SiteTextures::init(&sampling, nullptr, 1, 1);
        for (u32 map = 0; map < cMaps; ++map) {
            if (!sBuilt) {
                sImages[map][0] = redOf(map);
                sImages[map][1] = static_cast< u8 >(255 - 10 * map);
                sImages[map][2] = static_cast< u8 >(10 * map);
                sImages[map][3] = 255;
                sIds[map] = RenderTexture::linear(sImages[map], NEBULA_TEXTURE_LINEAR_RGBA8, 1, 1, 1);
            }
            material.setTextureId(map, sIds[map], sampling);
        }
        sBuilt = true;

        const f32 width = static_cast< f32 >(MR::getScreenWidth());
        const f32 height = static_cast< f32 >(NebulaFrameClock::cScreenLines);
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, height, 0.0f, width, -1.0f, 1.0f);
        Frame::setOrthographic(projection);
        {
            TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
            VertexP* vtx = site.vertices< VertexP >(NEBULA_PRIMITIVE_QUADS, 4);
            *vtx++ = {0.0f, 0.0f, 0.0f};
            *vtx++ = {width, 0.0f, 0.0f};
            *vtx++ = {width, height, 0.0f};
            *vtx++ = {0.0f, height, 0.0f};
        }

        NebulaScreenProbe probe;
        probe.x = -1.0f;
        probe.y = -1.0f;
        probe.user_x = 0.0f;
        probe.user_y = 0.0f;
        probe.user_z = 0.0f;
        probe.value = 0.0f;
        Vec2f at;
        MR::convertScreenPosToFrame(&at, Vec2f(width * 0.5f, height * 0.5f));
        probe.x = at.x;
        probe.y = at.y;
        Frame::probeScreen(cChannel, sAsker, &probe, 1);
    }
}  // namespace NebulaWideProbe
#endif
