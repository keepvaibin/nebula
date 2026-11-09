#pragma once

#include "Game/Render/FrameList.hpp"
#include "Game/Util/Color.hpp"
#include <NebulaGpu.h>
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstddef>
#include <string.h>

// How a site's vertices group; TypedDraw turns each into triangle indices (lines and points widen to triangles).
enum NebulaPrimitive {
    NEBULA_PRIMITIVE_QUADS = 0,          // drawn as (0 1 2)(0 2 3)
    NEBULA_PRIMITIVE_TRIANGLES = 1,
    NEBULA_PRIMITIVE_TRIANGLE_STRIP = 2, // odd triangles turned to keep the winding
    NEBULA_PRIMITIVE_TRIANGLE_FAN = 3,   // around vertex 0
    NEBULA_PRIMITIVE_LINES = 4,
    NEBULA_PRIMITIVE_LINE_STRIP = 5,
    NEBULA_PRIMITIVE_POINTS = 6
};

// A colour as a vertex holds it: red first.
struct Rgba8 {
    u8 r, g, b, a;

    Rgba8() = default;
    Rgba8(u8 red, u8 green, u8 blue, u8 alpha) : r(red), g(green), b(blue), a(alpha) {
    }
    // From R << 24 | G << 16 | B << 8 | A.
    explicit Rgba8(u32 word)
        : r(static_cast< u8 >(word >> 24)), g(static_cast< u8 >(word >> 16)), b(static_cast< u8 >(word >> 8)), a(static_cast< u8 >(word)) {
    }
    Rgba8(NebulaColor8 color) : r(color.r), g(color.g), b(color.b), a(color.a) {
    }
    Rgba8(const Color8& rColor) : r(rColor.r), g(rColor.g), b(rColor.b), a(rColor.a) {
    }
};

// Vertices a site writes: position first, what its shader reads after it. Each knows its layout.
struct VertexP {
    f32 x, y, z;
};
struct VertexPC {
    f32 x, y, z;
    Rgba8 color;
};
struct VertexPT {
    f32 x, y, z;
    f32 s, t;
};
struct VertexPCT {
    f32 x, y, z;
    Rgba8 color;
    f32 s, t;
};
struct VertexPN {
    f32 x, y, z;
    f32 nx, ny, nz;
};
struct VertexPNT {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 s, t;
};
struct VertexPNC {
    f32 x, y, z;
    f32 nx, ny, nz;
    Rgba8 color;
};
struct VertexPNCT {
    f32 x, y, z;
    f32 nx, ny, nz;
    Rgba8 color;
    f32 s, t;
};
struct VertexPT2 {
    f32 x, y, z;
    f32 s0, t0;
    f32 s1, t1;
};
struct VertexPCT2 {
    f32 x, y, z;
    Rgba8 color;
    f32 s0, t0;
    f32 s1, t1;
};
struct VertexPNT2 {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 s0, t0;
    f32 s1, t1;
};
struct VertexPCC {
    f32 x, y, z;
    Rgba8 color0;
    Rgba8 color1;
};
struct VertexPT3 {
    f32 x, y, z;
    f32 s0, t0;
    f32 s1, t1;
    f32 s2, t2;
};
struct VertexPCT3 {
    f32 x, y, z;
    Rgba8 color;
    f32 s0, t0;
    f32 s1, t1;
    f32 s2, t2;
};
struct VertexPNT3 {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 s0, t0;
    f32 s1, t1;
    f32 s2, t2;
};
struct VertexPCT4 {
    f32 x, y, z;
    Rgba8 color;
    f32 s0, t0;
    f32 s1, t1;
    f32 s2, t2;
    f32 s3, t3;
};

// A particle's corner: where it was a game frame ago rides in the normal's place.
struct VertexCorner {
    f32 x, y, z;
    f32 bx, by, bz;
    Rgba8 prm, env;
    f32 s, t;
};

template < class T >
struct VertexLayout;

#define NEBULA_INPUT3(semantic, type, field) {semantic, 0, 0, static_cast< u32 >(offsetof(type, field)), static_cast< u32 >(sizeof(type))}
#define NEBULA_INPUT2(semantic, type, field) {semantic, 0, 1, static_cast< u32 >(offsetof(type, field)), static_cast< u32 >(sizeof(type))}
#define NEBULA_INPUTC(semantic, type, field) {semantic, 0, 2, static_cast< u32 >(offsetof(type, field)), static_cast< u32 >(sizeof(type))}

template <>
struct VertexLayout< VertexP > {
    static constexpr u32 count = 0;
    static const NebulaLayoutInput* inputs() {
        return nullptr;
    }
};
template <>
struct VertexLayout< VertexPC > {
    static constexpr u32 count = 1;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPC, color)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPT > {
    static constexpr u32 count = 1;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPT, s)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPCT > {
    static constexpr u32 count = 2;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPCT, color), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPCT, s)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPN > {
    static constexpr u32 count = 1;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPN, nx)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPNT > {
    static constexpr u32 count = 2;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPNT, nx), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPNT, s)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPNC > {
    static constexpr u32 count = 2;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPNC, nx), NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPNC, color)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPNCT > {
    static constexpr u32 count = 3;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPNCT, nx), NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPNCT, color),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPNCT, s)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPT2 > {
    static constexpr u32 count = 2;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPT2, s0), NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPT2, s1)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPCT2 > {
    static constexpr u32 count = 3;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPCT2, color), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPCT2, s0),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPCT2, s1)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPNT2 > {
    static constexpr u32 count = 3;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPNT2, nx), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPNT2, s0),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPNT2, s1)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPCC > {
    static constexpr u32 count = 2;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPCC, color0), NEBULA_INPUTC(NEBULA_INPUT_COLOR1, VertexPCC, color1)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPT3 > {
    static constexpr u32 count = 3;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPT3, s0), NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPT3, s1),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 2, VertexPT3, s2)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPCT3 > {
    static constexpr u32 count = 4;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPCT3, color), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPCT3, s0),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPCT3, s1), NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 2, VertexPCT3, s2)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPNT3 > {
    static constexpr u32 count = 4;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexPNT3, nx), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPNT3, s0),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPNT3, s1), NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 2, VertexPNT3, s2)};
        return list;
    }
};
template <>
struct VertexLayout< VertexPCT4 > {
    static constexpr u32 count = 5;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexPCT4, color), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexPCT4, s0),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexPCT4, s1), NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 2, VertexPCT4, s2),
                                                 NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 3, VertexPCT4, s3)};
        return list;
    }
};
template <>
struct VertexLayout< VertexCorner > {
    static constexpr u32 count = 4;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[] = {NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexCorner, bx), NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexCorner, prm),
                                                 NEBULA_INPUTC(NEBULA_INPUT_COLOR1, VertexCorner, env), NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexCorner, s)};
        return list;
    }
};

// A particle corner for shapes that read coordinates 1 and 2.
struct VertexCornerExtras {
    f32 x, y, z;
    f32 bx, by, bz;
    Rgba8 prm, env;
    f32 s, t;
    f32 s1, t1;
    f32 q0, q1;
};

template <>
struct VertexLayout< VertexCornerExtras > {
    static constexpr u32 count = 6;
    static const NebulaLayoutInput* inputs() {
        static const NebulaLayoutInput list[count] = {
            NEBULA_INPUT3(NEBULA_INPUT_NORMAL, VertexCornerExtras, bx),
            NEBULA_INPUTC(NEBULA_INPUT_COLOR0, VertexCornerExtras, prm),
            NEBULA_INPUTC(NEBULA_INPUT_COLOR1, VertexCornerExtras, env),
            NEBULA_INPUT2(NEBULA_INPUT_COORD0, VertexCornerExtras, s),
            NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 1, VertexCornerExtras, s1),
            NEBULA_INPUT2(NEBULA_INPUT_COORD0 + 2, VertexCornerExtras, q0),
        };
        return list;
    }
};

namespace SiteTextures {
    void toFrame(const SiteTexture& rTexture, NebulaFrameTexture* pOut);
    // `pImage` at its size: linear filters, no levels.
    void init(SiteTexture* pTexture, const void* pImage, u32 width, u32 height, u8 wrapS = NEBULA_WRAP_CLAMP, u8 wrapT = NEBULA_WRAP_CLAMP);
    void setFilter(SiteTexture* pTexture, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias, bool edgeLod, u8 maxAniso);
}  // namespace SiteTextures

// Everything a site's draws take: a written shader, its constants and textures, its fixed state. A plain value the site
// fills and hands to a TypedDraw; nothing of it carries to another draw.
// Small constant sets stay inline; larger shaders and coordinate matrices grow.
class DrawConstants {
public:
    DrawConstants() : mData(mInline), mRoom(256) {}
    DrawConstants(const DrawConstants& rOther) : DrawConstants() { *this = rOther; }
    ~DrawConstants() { if (mData != mInline) { delete[] mData; } }
    DrawConstants& operator=(const DrawConstants& rOther) {
        if (this != &rOther) { ensure(rOther.mRoom); memcpy(mData, rOther.mData, rOther.mRoom * sizeof(f32)); }
        return *this;
    }
    void ensure(u32 count) {
        if (count <= mRoom) { return; }
        const u32 room = count > mRoom * 2 ? count : mRoom * 2;
        f32* pNew = new f32[room]();
        memcpy(pNew, mData, mRoom * sizeof(f32));
        if (mData != mInline) { delete[] mData; }
        mData = pNew; mRoom = room;
    }
    void clear(u32 count) { ensure(count); memset(mData, 0, mRoom * sizeof(f32)); }
    f32& operator[](u32 index) { ensure(index + 1); return mData[index]; }
    const f32& operator[](u32 index) const { return mData[index]; }
    operator f32*() { return mData; }
    operator const f32*() const { return mData; }
private:
    f32 mInline[256]{};
    f32* mData;
    u32 mRoom;
};

class DrawMatrixRows {
public:
    DrawMatrixRows() : mData(mInline), mCount(0), mRoom(16) {}
    DrawMatrixRows(const DrawMatrixRows& rOther) : DrawMatrixRows() { *this = rOther; }
    ~DrawMatrixRows() { if (mData != mInline) { delete[] mData; } }
    DrawMatrixRows& operator=(const DrawMatrixRows& rOther) {
        if (this != &rOther) { clear(); for (u32 i = 0; i < rOther.mCount; ++i) { add(rOther.mData[i]); } }
        return *this;
    }
    void clear() { mCount = 0; }
    void add(u32 row) {
        for (u32 i = 0; i < mCount; ++i) { if (mData[i] == row) { return; } }
        if (mCount == mRoom) {
            u32* pNew = new u32[mRoom * 2];
            memcpy(pNew, mData, mCount * sizeof(u32));
            if (mData != mInline) { delete[] mData; }
            mData = pNew; mRoom *= 2;
        }
        mData[mCount++] = row;
    }
    const u32* data() const { return mData; }
    u32 count() const { return mCount; }
private:
    u32 mInline[16];
    u32* mData;
    u32 mCount, mRoom;
};

// A site material's textures by slot: a few inline, more as slots are named; every entry zero until set.
class DrawTextures {
public:
    DrawTextures() : mData(mInline), mRoom(cInline) {
        memset(mInline, 0, sizeof(mInline));
    }
    DrawTextures(const DrawTextures& rOther) : DrawTextures() { *this = rOther; }
    ~DrawTextures() { if (mData != mInline) { delete[] mData; } }
    DrawTextures& operator=(const DrawTextures& rOther) {
        if (this != &rOther) {
            ensure(rOther.mRoom);
            memcpy(mData, rOther.mData, rOther.mRoom * sizeof(NebulaFrameTexture));
        }
        return *this;
    }
    // Room for `count` slots, the new ones zero.
    void ensure(u32 count) {
        if (count <= mRoom) { return; }
        const u32 room = count > mRoom * 2 ? count : mRoom * 2;
        NebulaFrameTexture* pNew = new NebulaFrameTexture[room];
        memset(pNew, 0, room * sizeof(NebulaFrameTexture));
        memcpy(pNew, mData, mRoom * sizeof(NebulaFrameTexture));
        if (mData != mInline) { delete[] mData; }
        mData = pNew; mRoom = room;
    }
    void clear() { memset(mData, 0, mRoom * sizeof(NebulaFrameTexture)); }
    NebulaFrameTexture& operator[](u32 slot) { ensure(slot + 1); return mData[slot]; }
    const NebulaFrameTexture& operator[](u32 slot) const { return mData[slot]; }
    operator const NebulaFrameTexture*() const { return mData; }
private:
    static const u32 cInline = 4;
    NebulaFrameTexture mInline[cInline];
    NebulaFrameTexture* mData;
    u32 mRoom;
};

struct SiteMaterial {
    enum Blend { BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADD_ALPHA, BLEND_SUBTRACT, BLEND_ADD };

    SiteMaterial() = default;
    // `depthFunc` NEBULA_COMPARE_* (NEBULA_COMPARE_ALWAYS with no write: depth off).
    SiteMaterial(NebulaShaderId shader, Blend blend, u8 depthFunc, bool depthWrite, u8 cull) {
        init(shader, blend, depthFunc, depthWrite, cull);
    }
    void init(NebulaShaderId shader, Blend blend, u8 depthFunc, bool depthWrite, u8 cull);

    // Coordinate matrix `index` among the constants the shader declares from row `first` (three rows each).
    void setMatrix(u32 first, u32 index, const f32 (*pMtx)[4]);
    void setTexture(u32 slot, const GameTexture* pTexture);
    void setTexture(u32 slot, const SiteTexture& rTexture);
    // A texture the host makes (`id`), sampled as `rSampling` says (its image is not used).
    void setTextureId(u32 slot, u32 id, const SiteTexture& rSampling);
    // Constants from float `at`: a colour as 0-1 values.
    void setColor(u32 at, u8 r, u8 g, u8 b, u8 a);
    void setDepth(bool test, u8 func, bool write) {
        PassStates::setDepth(mState, test, func, write);
    }
    void setBlend(u8 src, u8 dst) {
        PassStates::setBlend(mState, src, dst);
    }
    void setStencil(u8 test, u8 op) {
        mState.stencil_func = test;
        mState.stencil_op = op;
    }
    void takeState(const PassState& rState) {
        mState = rState;
    }

    NebulaShaderId mShader;
    PassState mState;
    DrawConstants constants;
    u32 mRowCount;   // rows of four floats the shader declares, or more for its coordinate matrices
    DrawMatrixRows mCoordRows;
    DrawTextures mTextures;
    u32 mTextureCount;
};

namespace FrameRing {
    void* vertices(u32 bytes, u32* pAt);
    u32* indices(u32 count, u32* pFirst);
    // Room for `count` rows of NEBULA_ITEM_INSTANCES draws' instances, the first's number in pFirst; how many the frame
    // has. A draw's instances are consecutive rows.
    void* instanceRows(u32 count, u32* pFirst);
    u32 instanceRowCount();
    // The frame's next instanced particle (written in place before the next is asked for).
    NebulaParticleInstance& particle();
}  // namespace FrameRing

class Mesh;
class ModelData;
class MaterialDraw;

// A site's draws: typed vertices in the frame's ring, under a matrix, with a material. Writing the vertices is the draw;
// `vertices` hands out room for `count` of them as `primitive` groups them.
class TypedDraw {
public:
    // Under the scene's camera at the world place `world` (null: the world's own).
    TypedDraw(const SiteMaterial& rMaterial, const f32 (*pWorld)[4]);
    // Under a camera other than the one models draw in now.
    TypedDraw(const SiteMaterial& rMaterial, const f32 (*pWorld)[4], const f32 (*pCamera)[4]);
    // Through a cooked material's programs and values, as `rMaterial` holds them now.
    TypedDraw(MaterialDraw& rMaterial, const f32 (*pWorld)[4], const f32 (*pCamera)[4]);
    ~TypedDraw();

    // `pEye` in eye space already (a view the site made): moves as a whole from frame to frame.
    static TypedDraw eyeSpace(const SiteMaterial& rMaterial, const f32 (*pEye)[4]);
    // With no material yet: `setForm` gives it.
    static TypedDraw eyeSpace(const f32 (*pEye)[4]);

    void setMaterial(const SiteMaterial& rMaterial);
    // A cooked material's form: its programs, the inputs they read, its values and textures, a state word.
    void setForm(u64 vertex, u64 pixel, const u8* pInputs, u32 inputCount, u32 constants, u32 table, u32 stateWord);
    void setWorld(const f32 (*pWorld)[4]);
    void setEye(const f32 (*pEye)[4]);
    void setSource(u32 hash) {
        mSource = hash;
    }
    // Normals by the inverse transpose of the matrix, for a place that scales.
    void setNormalsScaled() {
        finish();
        mNormalsScaled = true;
        mBlockKnown = false;
    }
    // Item flags for the draws after this.
    void setFlags(u32 flags) {
        mFlags = flags;
    }

    // A mesh of a model file, or all of them, under the site's matrix, with the site's material.
    void drawMesh(Mesh* pMesh);
    void drawModelFile(ModelData* pData);
    // The model file's matrices for the next meshes, by joint.
    void setModelJoints(const ModelData* pData, const f32 (*pJoints)[4]);
    // The block the next item draws under.
    u32 currentBlock() {
        finish();
        return block();
    }
    // Model-file meshes under a block that already holds the model's joints.
    void setModelBlock(const ModelData* pData, u32 modelBlock) {
        finish();
        mJointsOf = pData;
        mBlock = modelBlock;
        mBlockKnown = true;
        mEye = false;
    }
    void setBlock(u32 block) {
        finish();
        mBlock = block;
        mBlockKnown = true;
    }
    // A registered site mesh of `indexCount` indices under the site's matrix.
    void drawSiteMesh(u32 mesh, u32 indexCount, u32 stride, const NebulaLayoutInput* pInputs, u32 inputCount, u32 flags);
    // `count` instanced particles from instance row `firstRow`, `vertices` each, through the particle material.
    void drawParticles(u32 firstRow, u32 count, u32 vertices);
    // `count` NebulaGlyphInstance from instance row `firstRow`, through the font material.
    void drawGlyphs(u32 firstRow, u32 count);
    // `count` instances of `rows` rows from instance row `firstRow`, `vertices` each, through `pipeline`.
    void drawInstances(u32 pipeline, u32 firstRow, u32 count, u32 rows, u32 vertices);

    template < class T >
    T* vertices(NebulaPrimitive primitive, u32 count) {
        finish();
        return static_cast< T* >(begin(primitive, count, sizeof(T), VertexLayout< T >::inputs(), VertexLayout< T >::count));
    }

    // Room for `count` vertices of `stride` bytes each, read through `inputCount` inputs that stay valid for the frame:
    // for vertices whose layout is only known as the site runs.
    void* vertices(NebulaPrimitive primitive, u32 count, u32 stride, const NebulaLayoutInput* pInputs, u32 inputCount) {
        finish();
        return begin(primitive, count, stride, pInputs, inputCount);
    }

    // Writes nothing and draws what was written so far.
    void finish();

private:
    struct EyeTag {};
    TypedDraw(const SiteMaterial& rMaterial, const f32 (*pEye)[4], EyeTag);
    TypedDraw(const f32 (*pEye)[4], EyeTag);
    void* begin(NebulaPrimitive primitive, u32 count, u32 stride, const NebulaLayoutInput* pInputs, u32 inputCount);
    void widen();
    u32 block();
    u32 pipelineOf(const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride, bool wide) const;
    void clearFields();

    u64 mFormVertex;
    u64 mFormPixel;
    const u8* mFormInputs;
    u32 mFormInputCount;
    const NebulaLayoutInput* mLayout;
    u32 mLayoutCount;
    u32 mConstants;
    u32 mTable;
    PassState mState;
    NebulaShaderId mShader;
    u32 mBlock;
    bool mBlockKnown;
    bool mNormalsScaled;
    const ModelData* mJointsOf;
    const f32 (*mJoints)[4];
    bool mEye;
    Mtx mMtx;
    u32 mCamera;
    u32 mSource;
    u32 mFlags;
    u32 mPart;
    NebulaPrimitive mPrimitive;
    u32 mCount;
    u32 mStride;
    u32 mRingAt;
    u32 mItem;
    bool mOpen;
};

// Geometry a site builds once and draws for a long time: handed to the renderer, which keeps its own copy. The arena
// holding the mesh destroys it, if its owner has not, and the renderer's copy goes with it.
class SiteMeshData : public ArenaOwned {
public:
    SiteMeshData();
    ~SiteMeshData() override;
    SiteMeshData(const SiteMeshData&) = delete;
    SiteMeshData& operator=(const SiteMeshData&) = delete;
    // Hands the geometry written over and frees the writing room.
    void send();

protected:
    void initData(u32 stride, u32 maxVertices);
    void initIndexedData(u32 stride, u32 vertexCount, u32 indexCount, void** ppVertices, u32** ppIndices);
    void* stripData(u32 count);
    void drawData(TypedDraw& rDraw, const NebulaLayoutInput* pInputs, u32 inputCount, u32 flags) const;

private:
    u32 mId;
    u32 mIndexCount;
    u32 mStride;
    u32 mVertexCount;
    u32 mWritten;
    u8* mVertices;
    u32* mIndices;
};

template < class T >
class SiteMesh : public SiteMeshData {
public:
    // A triangle list of known size: both written over before `send`.
    void initIndexed(u32 vertexCount, u32 indexCount, T** ppVertices, u32** ppIndices) {
        void* pVertices;
        initIndexedData(sizeof(T), vertexCount, indexCount, &pVertices, ppIndices);
        *ppVertices = static_cast< T* >(pVertices);
    }
    void init(u32 maxVertices) {
        initData(sizeof(T), maxVertices);
    }
    // Room for a triangle strip of `count` vertices.
    T* strip(u32 count) {
        return static_cast< T* >(stripData(count));
    }
    void draw(TypedDraw& rDraw) const {
        drawData(rDraw, VertexLayout< T >::inputs(), VertexLayout< T >::count, NEBULA_ITEM_ONE_MATRIX);
    }
    // The vertices carry their own matrix numbers, read through the site's block.
    void drawJointed(TypedDraw& rDraw) const {
        drawData(rDraw, VertexLayout< T >::inputs(), VertexLayout< T >::count, 0);
    }
};

// Sites of matrices the game keeps as its current view.
namespace ModelSys {
    u32 currentCamera();
}  // namespace ModelSys

extern const Mtx cIdentityMtx;
