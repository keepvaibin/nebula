#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    void setColor(u8* pOut, const NebulaColor8& rColor) {
        pOut[0] = rColor.r;
        pOut[1] = rColor.g;
        pOut[2] = rColor.b;
        pOut[3] = rColor.a;
    }
}  // namespace

void J3DStateList::reserve(u16 capacity) {
    mOps = new NebulaStateOp[capacity];
    mCount = 0;
    mCapacity = capacity;
}

void J3DStateList::copy(const J3DStateList& rOther) {
    if (mCapacity < rOther.mCount) {
        reserve(rOther.mCount);
    }
    if (rOther.mCount != 0) {
        memcpy(mOps, rOther.mOps, sizeof(NebulaStateOp) * rOther.mCount);
    }
    mCount = rOther.mCount;
    mMaterialPass = rOther.mMaterialPass;
}

namespace {
    // The game's own changes have kinds of their own (J3D_STATE_*): folded here, never sent.
    bool gameOnly(u8 kind) {
        return kind >= J3D_STATE_BLEND_ETC;
    }
}  // namespace

void J3DStateList::send() const {
    if (isEmpty()) {
        return;
    }
    J3DNativeShape::flushState();
    J3DNativeMaterial::state(mOps, mCount);
    J3DNativeState::passChanges(mOps, mCount);
    if (mMaterialPass != 0) { J3DNativeMaterial::applyPass(mMaterialPass); }
    // The host's share, in order, in runs between material-only changes.
    const NebulaHostServices* pHost = nebula_host();
    u16 start = 0;
    for (u16 i = 0; i <= mCount; i++) {
        if (i == mCount || gameOnly(mOps[i].kind)) {
            if (i > start) {
                pHost->state(pHost->user, mOps + start, i - start);
            }
            start = i + 1;
        }
    }
}

NebulaStateOp& J3DStateList::add(u8 kind) {
    if (mCount == mCapacity) {
        NEBULA_PANIC("J3DStateList: more changes than its storage holds");
    }
    NebulaStateOp& rOp = mOps[mCount++];
    memset(&rOp, 0, sizeof(rOp));
    rOp.kind = kind;
    return rOp;
}

namespace {
    // GpuBlendFactor as the renderer's factor (NEBULA_FACTOR_*): the console's colour factors name the other side's colour (a
    // source factor 2 is the destination's colour).
    u8 nativeFactor(u8 gx, bool source) {
        if (gx == 2 || gx == 3) {
            return source ? static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_INV_DST_COLOR)
                          : static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_SRC_COLOR : NEBULA_FACTOR_INV_SRC_COLOR);
        }
        return gx;
    }
    // GpuBlendMode as the blend the renderer performs (NEBULA_BLEND_*).
    u8 nativeBlendOp(GpuBlendMode type) {
        return type == GPU_BM_BLEND ? NEBULA_BLEND_ADD : type == GPU_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
           : type == GPU_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
    }
}  // namespace

void J3DStateList::setBlendModeEtc(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op,
                                   GpuBool colorUpdate, GpuBool alphaUpdate) {
    NebulaStateOp& r = add(J3D_STATE_BLEND_ETC);
    r.a[0] = nativeBlendOp(type);
    r.a[1] = nativeFactor(src, true);
    r.a[2] = nativeFactor(dst, false);
    r.a[3] = op;
    r.a[4] = colorUpdate;
    r.a[5] = alphaUpdate;
}

void J3DStateList::setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op) {
    NebulaStateOp& r = add(J3D_STATE_BLEND);
    r.a[0] = nativeBlendOp(type);
    r.a[1] = nativeFactor(src, true);
    r.a[2] = nativeFactor(dst, false);
    r.a[3] = op;
}

void J3DStateList::setZMode(GpuBool compare, GpuCompare func, GpuBool update) {
    NebulaStateOp& r = add(J3D_STATE_Z_MODE);
    r.a[0] = compare;
    r.a[1] = func;
    r.a[2] = update;
}

void J3DStateList::setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1) {
    NebulaStateOp& r = add(J3D_STATE_ALPHA_COMPARE);
    r.a[0] = comp0;
    r.a[1] = ref0;
    r.a[2] = op;
    r.a[3] = comp1;
    r.a[4] = ref1;
}

void J3DStateList::setDstAlpha(GpuBool enable, u8 alpha) {
    NebulaStateOp& r = add(J3D_STATE_DST_ALPHA);
    r.a[0] = enable;
    r.a[1] = alpha;
}

namespace {
    // GpuCullMode as the renderer's (NEBULA_CULL_*: the game's own winding).
    u8 nativeCull(GpuCullMode mode) {
        return mode == GPU_CULL_BACK ? NEBULA_CULL_BACK : mode == GPU_CULL_FRONT ? NEBULA_CULL_FRONT
           : mode == GPU_CULL_ALL ? NEBULA_CULL_ALL : NEBULA_CULL_NONE;
    }
}  // namespace

void J3DStateList::setCullMode(GpuCullMode mode) {
    add(J3D_STATE_CULL).a[0] = nativeCull(mode);
}

void J3DStateList::setTevColor(GpuTevRegID reg, NebulaColor8 color) {
    NebulaStateOp& r = add(J3D_STATE_TEV_COLOR);
    r.a[0] = reg;
    setColor(r.a + 1, color);
}

void J3DStateList::setChanMatColor(GpuChannelID chan, NebulaColor8 color) {
    NebulaStateOp& r = add(J3D_STATE_MATERIAL_COLOR);
    r.a[0] = static_cast< u8 >(chan & 1);  // the channel (0 or 1); the whole colour
    setColor(r.a + 1, color);
}

void J3DStateList::setChanAmbColor(GpuChannelID chan, NebulaColor8 color) {
    NebulaStateOp& r = add(J3D_STATE_AMBIENT_COLOR);
    r.a[0] = static_cast< u8 >(chan & 1);  // the channel (0 or 1); the whole colour
    setColor(r.a + 1, color);
}

void J3DStateList::setTexCoordGen(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, GpuBool normalize,
                                  u32 postMtx) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_COORD_GEN);
    r.a[0] = coord;
    r.a[1] = J3DNativeTexGen::kind(type, src);
    r.a[2] = J3DNativeTexGen::source(src);
    r.a[3] = normalize;
    r.a[4] = J3DNativeTexGen::post(postMtx);
}

void J3DStateList::setTexLookupMode(GpuTexMapID map, GpuTexWrapMode wrapS, GpuTexWrapMode wrapT, GpuTexFilter minFilter,
                                    GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GpuBool biasClamp,
                                    GpuBool edgeLod, GpuAnisotropy maxAniso) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_LOOKUP);
    r.a[0] = map;
    r.a[1] = wrapS;
    r.a[2] = wrapT;
    r.a[3] = minFilter == GPU_LINEAR || minFilter == GPU_LIN_MIP_NEAR || minFilter == GPU_LIN_MIP_LIN;
    r.a[4] = magFilter != GPU_NEAR;
    r.a[5] = minFilter < GPU_NEAR_MIP_NEAR ? 0 : minFilter < GPU_NEAR_MIP_LIN ? 1 : 2;
    r.a[6] = biasClamp;
    r.a[7] = edgeLod;
    r.a[8] = maxAniso;
    r.f[0] = minLod;
    r.f[1] = maxLod;
    r.f[2] = lodBias;
}

void J3DStateList::setTexImgPtr(GpuTexMapID map, const void* pImage) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_IMAGE);
    r.a[0] = map;
    r.image = static_cast< u64 >(reinterpret_cast< uintptr_t >(pImage));
}

void J3DStateList::setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
    NebulaStateOp& r = add(J3D_STATE_FOG);
    r.a[0] = type;
    setColor(r.a + 1, color);
    r.f[0] = start;
    r.f[1] = end;
    r.f[2] = nearZ;
    r.f[3] = farZ;
}

void J3DStateList::setColorUpdate(GpuBool enable) {
    add(J3D_STATE_COLOR_UPDATE).a[0] = enable;
}

void J3DStateList::setAlphaUpdate(GpuBool enable) {
    add(J3D_STATE_ALPHA_UPDATE).a[0] = enable;
}

void J3DStateList::setTevKColor(GpuTevKColorID id, NebulaColor8 color) {
    NebulaStateOp& r = add(J3D_STATE_TEV_KCOLOR);
    r.a[0] = id;
    setColor(r.a + 1, color);
}

void J3DStateList::setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize,
                                   u32 postMtx) {
    setTexCoordGen(coord, type, src, normalize, postMtx);
    NebulaStateOp& r = mOps[mCount - 1];
    r.a[5] = 1;
    // the console's matrix id (three rows a matrix: GPU_PNMTX0-9 0-27, GPU_TEXMTX0-9 30-57, GPU_IDENTITY 60) as its slot.
    r.a[6] = static_cast< u8 >(mtx / 3);
}

void J3DStateList::setCurrentMtx(u32 id) {
    add(J3D_STATE_CURRENT_MATRIX).a[0] = static_cast< u8 >(id / 3);  // GX_PNMTXn is 3n
}

namespace {
    // GpuTexOffset to the n of a 1/n-texel coordinate offset (0: none; 1/16 to 1, then 1).
    const u8 sTexOffsetDivisor[8] = {0, 16, 8, 4, 2, 1, 1, 1};
}  // namespace

// the console's widths are sixths of a pixel: the op carries pixels.
void J3DStateList::setLineWidth(u8 width, GpuTexOffset texOffsets) {
    NebulaStateOp& r = add(J3D_STATE_LINE_WIDTH);
    r.f[0] = width / 6.0f;
    r.a[1] = sTexOffsetDivisor[texOffsets & 7];
}

void J3DStateList::setPointSize(u8 size, GpuTexOffset texOffsets) {
    NebulaStateOp& r = add(J3D_STATE_POINT_SIZE);
    r.f[0] = size / 6.0f;
    r.a[1] = sTexOffsetDivisor[texOffsets & 7];
}

void J3DStateList::setLight(GpuLightID id, const J3DNativeLight& rLight) {
    // The light's number: its id's highest bit (GpuLoadLightObjImm).
    u8 index = 0;
    for (u32 bits = id; bits > 1; bits >>= 1) {
        index++;
    }
    NebulaStateOp& rAttn = add(NEBULA_STATE_LIGHT_ATTENUATION);
    rAttn.a[0] = index & 7;
    memcpy(rAttn.a + 1, rLight.mColor, 4);
    for (u32 i = 0; i < 3; i++) {
        rAttn.f[i] = rLight.mAngle[i];
        rAttn.f[3 + i] = rLight.mDistance[i];
    }
    NebulaStateOp& rPos = add(NEBULA_STATE_LIGHT_POSITION);
    rPos.a[0] = index & 7;
    for (u32 i = 0; i < 3; i++) {
        rPos.f[i] = rLight.mPosition[i];
        rPos.f[3 + i] = rLight.mDirection[i];
    }
}

void J3DStateList::setFrameAlpha(bool keepAlpha, bool changed) {
    NebulaStateOp& r = add(NEBULA_STATE_FRAME_ALPHA);
    r.a[0] = changed;
    r.a[1] = keepAlpha;
}

void J3DStateList::enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points) {
    NebulaStateOp& r = add(J3D_STATE_TEX_OFFSETS);
    r.a[0] = coord;
    r.a[1] = lines;
    r.a[2] = points;
}

void J3DStateList::setTevColorS10(GpuTevRegID id, NebulaColor16 color) {
    NebulaStateOp& r = add(J3D_STATE_TEV_COLOR_S10);
    r.a[0] = id;
    r.size[0] = static_cast< u16 >(color.r);
    r.size[1] = static_cast< u16 >(color.g);
    r.f[0] = color.b;
    r.f[1] = color.a;
}
