#pragma once

#include <nebula_game_api.h>
#include <nebula_cooked_material.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

// Changes the game keeps for itself (J3DStateList::send folds them into its materials and its pass state; the host never
// sees them), beside the ones it sends (NEBULA_STATE_*).
enum {
    J3D_STATE_BLEND_ETC = 100,      /* type, source, destination, logic op, colour update, alpha update */
    J3D_STATE_BLEND,                /* type, source, destination, logic op (updates kept) */
    J3D_STATE_Z_MODE,               /* compare, function, update */
    J3D_STATE_ALPHA_COMPARE,        /* comparison 0, reference 0, op, comparison 1, reference 1 */
    J3D_STATE_DST_ALPHA,            /* enable, alpha */
    J3D_STATE_CULL,                 /* NEBULA_CULL_* */
    J3D_STATE_TEV_COLOR,            /* register, r, g, b, a */
    J3D_STATE_MATERIAL_COLOR,       /* channel (0, 1), r, g, b, a */
    J3D_STATE_AMBIENT_COLOR,        /* as NEBULA_STATE_MATERIAL_COLOR */
    J3D_STATE_FOG,                  /* type, r, g, b, a; f: start, end, near, far */
    J3D_STATE_COLOR_UPDATE,         /* enable */
    J3D_STATE_ALPHA_UPDATE,         /* enable */
    J3D_STATE_TEV_KCOLOR,           /* konst register, r, g, b, a */
    J3D_STATE_TEV_COLOR_S10,        /* register; size: r, g; f: b, a (signed 10-bit values) */
    J3D_STATE_IND_TEX_MATRIX,       /* matrix id (GXIndTexMtxID), scale exponent (s8); f: offset[0][0], [1][0],
                                       [0][1], [1][1], [0][2], [1][2] */
    J3D_STATE_CURRENT_MATRIX,       /* position/normal matrix (0-9) */
    J3D_STATE_LINE_WIDTH,           /* f: width (pixels); a[1]: texture offset n (1/n texel; 0 none) */
    J3D_STATE_POINT_SIZE,           /* f: size (pixels); a[1]: texture offset n (1/n texel; 0 none) */
    J3D_STATE_TEX_OFFSETS,          /* texture coordinate, lines offset it, points offset it */
};

// Drawing state the game sets outside materials, as values (nebula_game_api.h NebulaStateOp), sent as one record in
// draw order. Each setter takes the arguments of the console function its change is named after.
class J3DStateList {
public:
    J3DStateList() : mOps(nullptr), mCount(0), mCapacity(0), mMaterialPass(0) {}
    // A list writing into `pStorage` (`capacity` changes), for one being built.
    J3DStateList(NebulaStateOp* pStorage, u16 capacity) : mOps(pStorage), mCount(0), mCapacity(capacity), mMaterialPass(0) {}

    // Storage for `capacity` changes, from the current heap.
    void reserve(u16 capacity);
    // `rOther`'s changes (storage sized to them from the current heap the first time).
    void copy(const J3DStateList& rOther);
    void clear() { mCount = 0; mMaterialPass = 0; }
    // The first `count` changes of its storage, written there directly.
    void adopt(u16 count) { mCount = count; }
    bool isEmpty() const { return mCount == 0 && mMaterialPass == 0; }
    const NebulaStateOp* ops() const { return mOps; }
    u16 count() const { return mCount; }
    // The changes, as a record after every command written before it.
    void send() const;
    void setMaterialPass(NebulaMaterialPass pass) { mMaterialPass = pass; }

    void setBlendModeEtc(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op, GpuBool colorUpdate,
                         GpuBool alphaUpdate);
    void setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
    void setZMode(GpuBool compare, GpuCompare func, GpuBool update);
    void setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1);
    void setDstAlpha(GpuBool enable, u8 alpha);
    void setCullMode(GpuCullMode mode);
    void setTevColor(GpuTevRegID reg, NebulaColor8 color);
    void setChanMatColor(GpuChannelID chan, NebulaColor8 color);
    void setChanAmbColor(GpuChannelID chan, NebulaColor8 color);
    void setTexCoordGen(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, GpuBool normalize, u32 postMtx);
    void setTexLookupMode(GpuTexMapID map, GpuTexWrapMode wrapS, GpuTexWrapMode wrapT, GpuTexFilter minFilter,
                          GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GpuBool biasClamp,
                          GpuBool edgeLod, GpuAnisotropy maxAniso);
    void setTexImgPtr(GpuTexMapID map, const void* pImage);
    // A colour-indexed texture's palette: its format and place (a console TLUT name).
    void setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    // Changes of some fields of a register, its others as they are (the console's partial setters).
    void enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points);
    void setColorUpdate(GpuBool enable);
    void setAlphaUpdate(GpuBool enable);
    void setTevKColor(GpuTevKColorID id, NebulaColor8 color);
    void setTevColorS10(GpuTevRegID id, NebulaColor16 color);
    // GpuSetTevOp's preset as its four changes (inputs and operations of both halves).
    // GpuSetTexCoordGen2's change: setTexCoordGen's and the coordinate's matrix index.
    void setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
    void setCurrentMtx(u32 id);
    // GpuSetTevIndWarp's setTevIndirect.
    void setLineWidth(u8 width, GpuTexOffset texOffsets);
    void setPointSize(u8 size, GpuTexOffset texOffsets);
    // A light (GpuLoadLightObjImm's: its attenuation and colour, then its position and direction).
    void setLight(GpuLightID id, const J3DNativeLight& rLight);
    // Whether the frame keeps alpha; `changed` when it differs from the last value set.
    void setFrameAlpha(bool keepAlpha, bool changed);
    // GpuSetFogRangeAdj's changes: an enabled one's table pairs, then the centre and enable.

private:
    NebulaStateOp& add(u8 kind);

    NebulaStateOp* mOps;
    u16 mCount;
    u16 mCapacity;
    u8 mMaterialPass;
};
