#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <math.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    NebulaPassState sPass;

}  // namespace

namespace J3DNativeState {
    const NebulaPassState* pass() {
        sPass.position_matrix = J3DNativeShape::currentPosition();
        return &sPass;
    }

    void sendMaterial(const NebulaNativeMaterial* pMaterial) {
        J3DNativeShape::flushState();
        // Its state comes with the material except when only its constants change, or where it keeps what was set.
        if ((pMaterial->constants_only & 1) == 0 && pMaterial->shader != NEBULA_SHADER_NONE) {
            if (pMaterial->blend_op != NEBULA_NATIVE_KEPT) {
                sPass.blend_op = pMaterial->blend_op;
                sPass.blend_src = pMaterial->blend_src <= 9 ? pMaterial->blend_src : 0;
                sPass.blend_dst = pMaterial->blend_dst <= 9 ? pMaterial->blend_dst : 0;
            }
            if (pMaterial->color_write != 2) {
                sPass.color_write = pMaterial->color_write != 0;
            }
            if (pMaterial->alpha_write != 2) {
                sPass.alpha_write = pMaterial->alpha_write != 0;
            }
            if (pMaterial->depth_test != NEBULA_NATIVE_KEPT) {
                sPass.depth_test = pMaterial->depth_test != 0;
                sPass.depth_func = pMaterial->depth_func & 7;
                sPass.depth_write = pMaterial->depth_write != 0;
            }
            if (pMaterial->cull != NEBULA_NATIVE_KEPT) {
                sPass.cull = pMaterial->cull <= 2 ? pMaterial->cull : 0;
            }
        }
        const NebulaHostServices* pHost = nebula_host();
        pHost->native_material(pHost->user, pMaterial);
    }

    void initLightColor(J3DNativeLight* pObj, NebulaColor8 color) {
        J3DNativeLight& rLight = *pObj;
        rLight.color[0] = color.r;
        rLight.color[1] = color.g;
        rLight.color[2] = color.b;
        rLight.color[3] = color.a;
    }

    void initLightPos(J3DNativeLight* pObj, f32 x, f32 y, f32 z) {
        J3DNativeLight& rLight = *pObj;
        rLight.position[0] = x;
        rLight.position[1] = y;
        rLight.position[2] = z;
    }

    void initLightDir(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz) {
        J3DNativeLight& rLight = *pObj;
        rLight.direction[0] = -nx;
        rLight.direction[1] = -ny;
        rLight.direction[2] = -nz;
    }

    // The distance of a light at infinity.
    const f32 cInfiniteLightDistance = 1.0e18f;

    void initSpecularDir(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz) {
        const f32 hx = -nx;
        const f32 hy = -ny;
        const f32 hz = 1.0f - nz;
        f32 mag = hx * hx + hy * hy + hz * hz;
        if (mag != 0.0f) {
            mag = 1.0f / sqrtf(mag);
        }
        initSpecularDirHA(pObj, nx, ny, nz, hx * mag, hy * mag, hz * mag);
    }

    void initSpecularDirHA(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {
        J3DNativeLight& rLight = *pObj;
        rLight.direction[0] = hx;
        rLight.direction[1] = hy;
        rLight.direction[2] = hz;
        rLight.position[0] = nx * -cInfiniteLightDistance;
        rLight.position[1] = ny * -cInfiniteLightDistance;
        rLight.position[2] = nz * -cInfiniteLightDistance;
    }

    void initLightAttn(J3DNativeLight* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
        J3DNativeLight& rLight = *pObj;
        rLight.angle[0] = a0;
        rLight.angle[1] = a1;
        rLight.angle[2] = a2;
        rLight.distance[0] = k0;
        rLight.distance[1] = k1;
        rLight.distance[2] = k2;
    }

    void initLightNoSpot(J3DNativeLight* pObj) {
        pObj->angle[0] = 1.0f;
        pObj->angle[1] = 0.0f;
        pObj->angle[2] = 0.0f;
    }

    void initLightDistAttn(J3DNativeLight* pObj, f32 refDistance, f32 refBrightness, J3DLightFalloff falloff) {
        if (refDistance < 0.0f || refBrightness <= 0.0f || refBrightness >= 1.0f) {
            falloff = J3D_FALLOFF_NONE;
        }
        f32 k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
        switch (falloff) {
        case J3D_FALLOFF_GENTLE:
            k1 = (1.0f - refBrightness) / (refBrightness * refDistance);
            break;
        case J3D_FALLOFF_MEDIUM:
            k1 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance);
            k2 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        case J3D_FALLOFF_STEEP:
            k2 = (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        default:
            break;
        }
        J3DNativeLight& rLight = *pObj;
        rLight.distance[0] = k0;
        rLight.distance[1] = k1;
        rLight.distance[2] = k2;
    }

    void sendLight(const J3DNativeLight* pLight, u8 index) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->light(pHost->user, index, pLight);
    }

    // The texture every map holds until the game loads its own: 4x4 opaque white, a texture record
    // (nebula_cooked_texture.h).
    static struct {
        NebulaTexture header;
        u8 texels[4 * 4 * 2];
    } sDefaultTexData ATTRIBUTE_ALIGN(32) = {NEBULA_TEXTURE_STATIC(4, 4, NEBULA_TEXTURE_LA8, 4 * 4 * 2),
                                             {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    // The projection before the game sets its own (orthographic, -1..1 depth).
    static const f32 sDefaultProjection[7] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f, 0.0f};
    // Until the game sets its own view.

    void initDefaults() {
        const NebulaColor8 black = {0, 0, 0, 0};

        for (u8 i = 0; i < 8; i++) {
            J3DPass::textureIdentity(i);
        }
        J3DPass::lineWidth(1.0f);
        J3DPass::pointSize(1.0f);

        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DNativeShape::setNormal(0, identity);
        J3DPass::positionMatrix(0);
        J3DNativeShape::setTexture(J3DNativeShape::kIdentityTexture, identity, 3);
        J3DNativeShape::setPostTexture(J3DNativeShape::kIdentityPost, identity);
        J3DNativeView::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        J3DNativeView::setProjectionv(sDefaultProjection);

        J3DPass::cull(NEBULA_CULL_BACK);
        J3DNativeView::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
        for (u8 channel = 0; channel < 2; channel++) {
            J3DNativeMaterial::setLightAmbient(channel, black);
        }

        J3DNativeTexture::registerRecord(&sDefaultTexData);

        J3DPass::noBlend();
        J3DPass::writeColor(true);
        J3DPass::writeAlpha(true);
        J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
    }

    void showFrame() {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_show(pHost->user);
    }

    void endFrame() {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_end(pHost->user);
    }
};  // namespace J3DNativeState
void J3DNativeStateInitLightColor(J3DNativeLight* pObj, NebulaColor8 color) {
    J3DNativeState::initLightColor(pObj, color);
}

void J3DNativeStateInitLightPos(J3DNativeLight* pObj, f32 x, f32 y, f32 z) {
    J3DNativeState::initLightPos(pObj, x, y, z);
}

void J3DNativeStateSendLight(const J3DNativeLight* pLight, u8 index) {
    J3DNativeState::sendLight(pLight, index);
}

namespace J3DPass {
    namespace {
        const u8 cFirstTextureSlot = 10;
        const u8 cIdentitySlot = 20;

        void sendCoordinate(u8 coord, u8 fromCoordinate, u8 matrixSlot) {
            J3DNativeShape::flushState();
            const NebulaHostServices* pHost = nebula_host();
            pHost->texture_coordinate(pHost->user, coord, fromCoordinate, matrixSlot);
        }
    }  // namespace

    void depth(bool test, u8 compare, bool write) {
        J3DNativeShape::flushState();
        sPass.depth_test = test;
        sPass.depth_func = compare & 7;
        sPass.depth_write = write;
    }

    void stencil(u8 test, u8 op) {
        J3DNativeShape::flushState();
        sPass.stencil_func = test;
        sPass.stencil_op = op;
    }

    void blendState(u8 op, u8 source, u8 destination) {
        J3DNativeShape::flushState();
        sPass.blend_op = op;
        sPass.blend_src = source <= 9 ? source : 0;
        sPass.blend_dst = destination <= 9 ? destination : 0;
    }

    void blend(u8 source, u8 destination) {
        blendState(NEBULA_BLEND_ADD, source, destination);
    }

    void noBlend() {
        blendState(NEBULA_BLEND_REPLACE, 0, 0);
    }

    void subtractBlend() {
        blendState(NEBULA_BLEND_SUBTRACT, 0, 0);
    }

    void cull(u8 faces) {
        J3DNativeShape::flushState();
        sPass.cull = faces & 3;
    }

    void writeColor(bool write) {
        J3DNativeShape::flushState();
        sPass.color_write = write;
    }

    void writeAlpha(bool write) {
        J3DNativeShape::flushState();
        sPass.alpha_write = write;
    }

    void positionMatrix(u8 index) {
        J3DNativeShape::flushState();
        J3DNativeShape::setCurrentPosition(index % 10);
    }

    void lineWidth(f32 pixels, bool spreadTexture) {
        J3DNativeShape::flushState();
        sPass.line_width = pixels * (NEBULA_FRAME_UNITS / 456.0f);  // authored for a picture this many lines tall
        sPass.line_divisor = spreadTexture ? 1 : 0;
        sPass.line_offsets = spreadTexture ? 7 : 0;
    }

    void pointSize(f32 pixels, bool spreadTexture) {
        J3DNativeShape::flushState();
        sPass.point_size = pixels * (NEBULA_FRAME_UNITS / 456.0f);  // authored for a picture this many lines tall
        sPass.point_divisor = spreadTexture ? 1 : 0;
        sPass.point_offsets = spreadTexture ? 7 : 0;
    }

    void textureMatrix(u8 coord, u8 matrix) {
        sendCoordinate(coord, 0, static_cast< u8 >(cFirstTextureSlot + matrix));
    }

    void textureIdentity(u8 coord) {
        sendCoordinate(coord, coord, cIdentitySlot);
    }
};  // namespace J3DPass

extern "C" {
void J3DPassDepth(int test, u8 compare, int write) {
    J3DPass::depth(test != 0, compare, write != 0);
}
void J3DPassBlend(u8 source, u8 destination) {
    J3DPass::blend(source, destination);
}
void J3DPassCull(u8 faces) {
    J3DPass::cull(faces);
}
void J3DPassWriteColor(int write) {
    J3DPass::writeColor(write != 0);
}
void J3DPassWriteAlpha(int write) {
    J3DPass::writeAlpha(write != 0);
}
void J3DPassPositionMatrix(u8 index) {
    J3DPass::positionMatrix(index);
}
}
