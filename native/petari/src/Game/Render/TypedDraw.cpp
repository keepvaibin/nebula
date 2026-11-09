#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_shader_rows.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace SiteTextures {
    void init(SiteTexture* pTexture, const void* pImage, u32 width, u32 height, u8 wrapS, u8 wrapT) {
        memset(pTexture, 0, sizeof(*pTexture));
        pTexture->mImage = pImage;
        pTexture->mWidth = width;
        pTexture->mHeight = height;
        pTexture->mWrapS = wrapS;
        pTexture->mWrapT = wrapT;
        pTexture->mMinFilter = NEBULA_FILTER_LINEAR;
        pTexture->mMagFilter = NEBULA_FILTER_LINEAR;
    }

    void setFilter(SiteTexture* pTexture, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias, bool, u8 maxAniso) {
        pTexture->mMinFilter = minFilter;
        pTexture->mMagFilter = magFilter;
        pTexture->mMinLod = minLod;
        pTexture->mMaxLod = maxLod;
        pTexture->mLodBias = lodBias;
        pTexture->mMaxAniso = maxAniso;
    }
}  // namespace SiteTextures

void SiteMaterial::init(NebulaShaderId shader, Blend blend, u8 depthFunc, bool depthWrite, u8 cull) {
    mCoordRows.clear();
    mTextureCount = 0;
    mTextures.clear();
    mShader = shader;
    mState = PassStates::plain();
    mState.blend_op = blend == BLEND_OPAQUE ? NEBULA_BLEND_REPLACE : blend == BLEND_SUBTRACT ? NEBULA_BLEND_SUBTRACT : NEBULA_BLEND_ADD;
    mState.blend_src = blend == BLEND_OPAQUE || blend == BLEND_SUBTRACT || blend == BLEND_ADD ? NEBULA_FACTOR_ONE : NEBULA_FACTOR_SRC_ALPHA;
    mState.blend_dst = blend == BLEND_ALPHA ? NEBULA_FACTOR_INV_SRC_ALPHA : blend == BLEND_OPAQUE ? NEBULA_FACTOR_ZERO : NEBULA_FACTOR_ONE;
    mState.depth_test = depthFunc != NEBULA_COMPARE_ALWAYS || depthWrite ? 1 : 0;
    mState.depth_func = depthFunc;
    mState.depth_write = depthWrite ? 1 : 0;
    mState.cull = cull;
    mRowCount = nebula_shader_rows[shader];
    constants.clear(mRowCount * 4);
}

void SiteMaterial::setShader(NebulaShaderId shader) {
    mShader = shader;
    const u32 rows = nebula_shader_rows[shader];
    if (mRowCount < rows) {
        mRowCount = rows;
    }
}

void SiteMaterial::setMatrix(u32 first, u32 index, const f32 (*pMtx)[4]) {
    constants.ensure((first + index * 3 + 3) * 4);
    memcpy(constants + (first + index * 3) * 4, pMtx, 12 * sizeof(f32));
    mCoordRows.add(first + index * 3);
    const u32 last = static_cast< u32 >(first + index * 3 + 3);
    if (mRowCount < last) {
        mRowCount = last;
    }
}

void SiteMaterial::setTexture(u32 slot, const GameTexture* pTexture) {
    setTexture(slot, pTexture->site());
}

void SiteTextures::toFrame(const SiteTexture& rTexture, NebulaFrameTexture* pOut) {
    NebulaFrameTexture& rOut = *pOut;
    memset(&rOut, 0, sizeof(rOut));
    const u8 minFilter = rTexture.mMinFilter;
    rOut.texture = RenderTexture::idOf(rTexture.mImage);
    rOut.wrap_s = rTexture.mWrapS;
    rOut.wrap_t = rTexture.mWrapT;
    rOut.mag_linear = rTexture.mMagFilter != 0 ? 1 : 0;
    rOut.min_linear = minFilter == 1 || minFilter == 3 || minFilter == 5 ? 1 : 0;
    rOut.mip_filter = minFilter < 2 ? 0 : minFilter < 4 ? 1 : 2;
    rOut.anisotropy = rTexture.mMaxAniso;
    rOut.lod_bias = rTexture.mLodBias;
    rOut.min_lod = rTexture.mMinLod;
    rOut.max_lod = rTexture.mMaxLod;
}

void SiteMaterial::setTexture(u32 slot, const SiteTexture& rTexture) {
    SiteTextures::toFrame(rTexture, &mTextures[slot]);
    if (mTextureCount <= slot) {
        mTextureCount = slot + 1;
    }
}

void SiteMaterial::setTextureId(u32 slot, u32 id, const SiteTexture& rSampling) {
    SiteTextures::toFrame(rSampling, &mTextures[slot]);
    mTextures[slot].texture = id;
    if (mTextureCount <= slot) {
        mTextureCount = slot + 1;
    }
}

void SiteMaterial::setColor(u32 at, u8 r, u8 g, u8 b, u8 a) {
    constants[at] = r / 255.0f;
    constants[at + 1] = g / 255.0f;
    constants[at + 2] = b / 255.0f;
    constants[at + 3] = a / 255.0f;
}

void TypedDraw::clearFields() {
    mOpen = false;
    mNormalsScaled = false;
    mJointsOf = nullptr;
    mJoints = nullptr;
    mFlags = 0;
    mSource = 0;
    mFormVertex = 0;
    mFormPixel = 0;
    mFormInputs = nullptr;
    mFormInputCount = 0;
}

TypedDraw::TypedDraw(const SiteMaterial& rMaterial, const f32 (*pWorld)[4]) {
    clearFields();
    mCamera = ModelSys::currentCamera();
    setMaterial(rMaterial);
    setWorld(pWorld);
}

TypedDraw::TypedDraw(const SiteMaterial& rMaterial, const f32 (*pWorld)[4], const f32 (*pCamera)[4]) {
    clearFields();
    mCamera = Frame::camera(pCamera, memcmp(pCamera, ModelSys::mViewMtx, sizeof(Mtx)) == 0 ? ModelSys::mViewRole : NEBULA_CAMERA_OTHER);
    setMaterial(rMaterial);
    setWorld(pWorld);
}

TypedDraw::TypedDraw(MaterialDraw& rMaterial, const f32 (*pWorld)[4], const f32 (*pCamera)[4]) {
    clearFields();
    mCamera = Frame::camera(pCamera, memcmp(pCamera, ModelSys::mViewMtx, sizeof(Mtx)) == 0 ? ModelSys::mViewRole : NEBULA_CAMERA_OTHER);
    rMaterial.bind(*this);
    setWorld(pWorld);
}

TypedDraw::TypedDraw(const SiteMaterial& rMaterial, const f32 (*pEye)[4], EyeTag) {
    clearFields();
    mCamera = NEBULA_NONE32;
    setMaterial(rMaterial);
    setEye(pEye);
}

TypedDraw TypedDraw::eyeSpace(const SiteMaterial& rMaterial, const f32 (*pEye)[4]) {
    return TypedDraw(rMaterial, pEye, EyeTag());
}

TypedDraw::TypedDraw(const f32 (*pEye)[4], EyeTag) {
    clearFields();
    mCamera = NEBULA_NONE32;
    mShader = static_cast< NebulaShaderId >(0);
    mState = PassStates::plain();
    mConstants = 0;
    mTable = 0;
    setEye(pEye);
}

TypedDraw TypedDraw::eyeSpace(const f32 (*pEye)[4]) {
    return TypedDraw(pEye, EyeTag());
}

TypedDraw::~TypedDraw() {
    finish();
}

void TypedDraw::setForm(u64 vertex, u64 pixel, const u8* pInputs, u32 inputCount, u32 constants, u32 table, u32 stateWord) {
    finish();
    mFormVertex = vertex;
    mFormPixel = pixel;
    mFormInputs = pInputs;
    mFormInputCount = inputCount;
    mConstants = constants;
    mTable = table;
    mState = PassStates::fromWord(stateWord);
}

u32 TypedDraw::pipelineOf(const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride, bool wide) const {
    const u32 state = Frame::finishState(mState.word());
    if (mFormVertex != 0) {
        return Frame::formPipeline(mFormVertex, mFormPixel, mFormInputs, mFormInputCount, state, pInputs, inputCount, stride);
    }
    return Frame::sitePipeline(mShader, state, pInputs, inputCount, stride, wide);
}

void TypedDraw::setMaterial(const SiteMaterial& rMaterial) {
    finish();
    mFormVertex = 0;
    mShader = rMaterial.mShader;
    mState = rMaterial.mState;
    const u32 rows = rMaterial.mRowCount;
    f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(rows * 16u, 16));
    memcpy(pRows, rMaterial.constants, rows * 16u);
    mConstants = Frame::addConstants(NEBULA_NONE32, 0, rows, pRows, rMaterial.mCoordRows.data(), rMaterial.mCoordRows.count());
    if (rMaterial.mTextureCount != 0) {
        const NebulaHostServices* pHost = nebula_host();
        mTable = pHost->texture_table(pHost->user, rMaterial.mTextures, rMaterial.mTextureCount);
    } else {
        mTable = 0;
    }
}

void TypedDraw::setWorld(const f32 (*pWorld)[4]) {
    finish();
    if (pWorld != nullptr) {
        memcpy(mMtx, pWorld, sizeof(mMtx));
    } else {
        NebulaMtxIdentity(mMtx);
    }
    mEye = false;
    mBlockKnown = false;
    mJointsOf = nullptr;
}

void TypedDraw::setEye(const f32 (*pEye)[4]) {
    finish();
    memcpy(mMtx, pEye, sizeof(mMtx));
    mEye = true;
    mBlockKnown = false;
    mJointsOf = nullptr;
}

void TypedDraw::setModelJoints(const ModelData* pData, const f32 (*pJoints)[4]) {
    finish();
    mJointsOf = pData;
    mJoints = pJoints;
    mEye = false;
    mBlockKnown = false;
}

void TypedDraw::drawMesh(Mesh* pMesh) {
    finish();
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = pMesh->mGpuMesh;
    rItem.pipeline = Frame::sitePipeline(mShader, Frame::finishState(mState.word()), pMesh->mFile);
    rItem.constants = mConstants;
    rItem.textures = mTable;
    rItem.block = block();
    rItem.slot = Frame::objectSlot();
    rItem.generation = Frame::objectGeneration();
    rItem.part = Frame::nextPart();
    rItem.source_hash = mSource;
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet(rItem.block);
    if (mJointsOf != nullptr) {
        rItem.matrix = pMesh->mFile->single_matrix != NEBULA_MODEL_NONE ? pMesh->mFile->single_matrix : 0;
        rItem.flags = static_cast< u32 >(mFlags | NEBULA_ITEM_PLAIN_MESH);
    } else {
        rItem.matrix = 0;
        rItem.flags = static_cast< u32 >(mFlags | NEBULA_ITEM_PLAIN_MESH | NEBULA_ITEM_ONE_MATRIX);
    }
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}

void TypedDraw::drawSiteMesh(u32 mesh, u32 indexCount, u32 stride, const NebulaLayoutInput* pInputs, u32 inputCount, u32 flags) {
    finish();
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = mesh;
    rItem.pipeline = pipelineOf(pInputs, inputCount, stride, false);
    rItem.constants = mConstants;
    rItem.textures = mTable;
    rItem.block = block();
    rItem.slot = Frame::objectSlot();
    rItem.generation = Frame::objectGeneration();
    rItem.part = Frame::nextPart();
    rItem.source_hash = mSource;
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet(rItem.block);
    rItem.matrix = 0;
    rItem.first = 0;
    rItem.count = indexCount;
    rItem.flags = static_cast< u32 >(mFlags | NEBULA_ITEM_INDEXED | flags);
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}

void TypedDraw::drawModelFile(ModelData* pData) {
    for (Material* pMaterial = pData->getJointAt(0)->getMesh(); pMaterial != nullptr; pMaterial = pMaterial->getNext()) {
        drawMesh(pMaterial->getMesh());
    }
}

u32 TypedDraw::block() {
    if (!mBlockKnown) {
        const u32 jointCount = mJointsOf != nullptr ? mJointsOf->getJointCount() : 1u;
        const f32 (*pSource)[4] = mJointsOf != nullptr ? mJoints : mMtx;
        f32 (*pJoint)[4] = static_cast< f32 (*)[4] >(Frame::arena(jointCount * sizeof(Mtx), 16));
        memcpy(pJoint, pSource, jointCount * sizeof(Mtx));
        u32* pFlags = Frame::alloc< u32 >(jointCount / 32u + 1u);
        memset(pFlags, 0xFF, (jointCount / 32u + 1u) * sizeof(u32));
        // A site's normals turn with its place alone: they are read through the place's rows.
        if (mNormalsScaled) {
            *pFlags = 0;
        }
        NebulaFrameBlock block;
        memset(&block, 0, sizeof(block));
        block.joints = pJoint;
        block.scale_flags = pFlags;
        block.joint_count = jointCount;
        block.model = mJointsOf != nullptr ? mJointsOf->mGpuModel : 0;
        block.slot = Frame::objectSlot();
        block.generation = Frame::objectGeneration();
        block.flags = static_cast< u32 >(mEye ? NEBULA_BLOCK_EYE_SPACE : 0);
        block.camera = mEye ? static_cast< u32 >(NEBULA_NONE32) : mCamera;
        block.calc_mode = mEye ? 1 : 0;
        block.normals_from = NEBULA_NONE32;
        mBlock = Frame::addBlock(block);
        mBlockKnown = true;
    }
    return mBlock;
}

void TypedDraw::drawParticles(u32 firstRow, u32 count, u32 vertices) {
    drawInstances(Frame::particlePipeline(Frame::finishState(mState.word())), firstRow, count,
                  sizeof(NebulaParticleInstance) / 16, vertices);
}

void TypedDraw::drawGlyphs(u32 firstRow, u32 count) {
    // A quad's two triangles.
    drawInstances(Frame::glyphPipeline(Frame::finishState(mState.word())), firstRow, count, sizeof(NebulaGlyphInstance) / 16, 6);
}

void TypedDraw::drawInstances(u32 pipeline, u32 firstRow, u32 count, u32 rows, u32 vertices) {
    finish();
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = rows;
    rItem.pipeline = pipeline;
    rItem.constants = mConstants;
    rItem.textures = mTable;
    rItem.block = block();
    rItem.slot = Frame::objectSlot();
    rItem.generation = Frame::objectGeneration();
    rItem.part = Frame::nextPart();
    rItem.source_hash = mSource;
    rItem.view = Frame::currentView();
    rItem.light_set = NEBULA_NONE32;
    rItem.matrix = 0;
    rItem.ring_at = firstRow;
    rItem.ring_count = count;
    rItem.first = 0;
    rItem.count = vertices;
    rItem.flags = static_cast< u32 >(NEBULA_ITEM_INSTANCES);
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}

namespace {
    // The indices that draw `count` vertices as triangles.
    u32 indexCount(NebulaPrimitive primitive, u32 count) {
        switch (primitive) {
        case NEBULA_PRIMITIVE_QUADS:
            return count / 4 * 6;
        case NEBULA_PRIMITIVE_TRIANGLE_STRIP:
        case NEBULA_PRIMITIVE_TRIANGLE_FAN:
            return count >= 3 ? (count - 2) * 3 : 0;
        default:
            return 0;
        }
    }

    void writeIndices(NebulaPrimitive primitive, u32 count, u32* pOut) {
        switch (primitive) {
        case NEBULA_PRIMITIVE_QUADS:
            for (u32 q = 0; q < count / 4; q++) {
                const u32 at = q * 4;
                *pOut++ = at;
                *pOut++ = at + 1;
                *pOut++ = at + 2;
                *pOut++ = at;
                *pOut++ = at + 2;
                *pOut++ = at + 3;
            }
            break;
        case NEBULA_PRIMITIVE_TRIANGLE_STRIP:
            for (u32 t = 0; t + 2 < count; t++) {
                const bool odd = (t & 1) != 0;
                *pOut++ = odd ? t + 1 : t;
                *pOut++ = odd ? t : t + 1;
                *pOut++ = t + 2;
            }
            break;
        case NEBULA_PRIMITIVE_TRIANGLE_FAN:
            for (u32 t = 0; t + 2 < count; t++) {
                *pOut++ = 0;
                *pOut++ = t + 1;
                *pOut++ = t + 2;
            }
            break;
        default:
            break;
        }
    }

    // What a widened corner carries after the site's vertex.
    struct WideTail {
        f32 ox, oy, oz;
        u32 widen;  // 1 point, 2 line; 4 its right edge, 8 its bottom edge
    };
    enum { Widen_Point = 1, Widen_Line = 2, Widen_Right = 4, Widen_Bottom = 8 };
}  // namespace

void* TypedDraw::begin(NebulaPrimitive primitive, u32 count, u32 stride, const NebulaLayoutInput* pInputs, u32 inputCount) {
    mPrimitive = primitive;
    mCount = count;
    mStride = stride;
    mLayout = pInputs;
    mLayoutCount = inputCount;
    void* pVertices = FrameRing::vertices(count * stride, &mRingAt);
    if (count == 0) {
        // Nothing to draw, but the number stays used: the draws after it keep theirs from frame to frame.
        Frame::nextPart();
        return pVertices;
    }
    // In the frame's order from here on, whatever else draws before the vertices are written.
    const bool wide = primitive == NEBULA_PRIMITIVE_LINES || primitive == NEBULA_PRIMITIVE_LINE_STRIP || primitive == NEBULA_PRIMITIVE_POINTS;
    mItem = Frame::itemCount();
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.constants = mConstants;
    rItem.textures = mTable;
    rItem.block = block();
    rItem.slot = Frame::objectSlot();
    rItem.source_hash = mSource;
    rItem.generation = Frame::objectGeneration();
    rItem.part = Frame::nextPart();
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet(rItem.block);
    if (wide) {
        mOpen = true;
        return pVertices;
    }
    rItem.mesh = stride;
    rItem.pipeline = pipelineOf(pInputs, inputCount, stride, false);
    rItem.ring_at = mRingAt;
    rItem.ring_count = count;
    const u32 indices = indexCount(primitive, count);
    if (indices != 0) {
        u32 first;
        writeIndices(primitive, count, FrameRing::indices(indices, &first));
        rItem.first = first;
        rItem.count = indices;
    } else {
        rItem.first = 0;
        rItem.count = count;
    }
    rItem.flags = static_cast< u32 >(mFlags | NEBULA_ITEM_RING | (indices != 0 ? NEBULA_ITEM_INDEXED : 0));
    return pVertices;
}

void TypedDraw::widen() {
    // Each line or point becomes a quad of corners: the site's vertex, the line's other end, which corner it is.
    const bool points = mPrimitive == NEBULA_PRIMITIVE_POINTS;
    const u32 segments = points ? mCount : mPrimitive == NEBULA_PRIMITIVE_LINES ? mCount / 2 : (mCount >= 2 ? mCount - 1 : 0);
    const u32 stride = mStride + sizeof(WideTail);
    u32 at;
    u8* pOut = static_cast< u8* >(FrameRing::vertices(segments * 4 * stride, &at));
    // The ring may have moved: find the site's vertices by their place.
    u8* pSource = pOut - at + mRingAt;
    const auto corner = [&](u32 vertex, u32 other, u32 widen) {
        memcpy(pOut, pSource + vertex * mStride, mStride);
        WideTail tail;
        memcpy(&tail.ox, pSource + other * mStride, sizeof(f32) * 3);
        tail.widen = widen;
        memcpy(pOut + mStride, &tail, sizeof(tail));
        pOut += stride;
    };
    for (u32 i = 0; i < segments; i++) {
        if (points) {
            corner(i, i, Widen_Point);
            corner(i, i, Widen_Point | Widen_Right);
            corner(i, i, Widen_Point | Widen_Right | Widen_Bottom);
            corner(i, i, Widen_Point | Widen_Bottom);
        } else {
            const u32 a = mPrimitive == NEBULA_PRIMITIVE_LINES ? i * 2 : i;
            const u32 b = a + 1;
            corner(a, b, Widen_Line);
            corner(a, b, Widen_Line | Widen_Right);
            corner(b, a, Widen_Line | Widen_Right);
            corner(b, a, Widen_Line);
        }
    }
    u32 first;
    u32* pIndices = FrameRing::indices(segments * 6, &first);
    writeIndices(NEBULA_PRIMITIVE_QUADS, segments * 4, pIndices);

    NebulaLayoutInput* inputs = Frame::layoutRoom(Frame::cLayoutRoomWiden, mLayoutCount + 2);
    for (u32 i = 0; i < mLayoutCount; i++) {
        inputs[i] = mLayout[i];
        inputs[i].stride = static_cast< u32 >(stride);
    }
    inputs[mLayoutCount] = {NEBULA_INPUT_TANGENT, 0, 0, static_cast< u32 >(mStride), static_cast< u32 >(stride)};
    inputs[mLayoutCount + 1] = {NEBULA_INPUT_WIDEN, 0, 3, static_cast< u32 >(mStride + 12), static_cast< u32 >(stride)};

    NebulaFrameItem& rItem = Frame::itemAt(mItem);
    rItem.mesh = stride;
    rItem.pipeline = pipelineOf(inputs, mLayoutCount + 2, stride, true);
    rItem.ring_at = at;
    rItem.ring_count = segments * 4;
    rItem.first = first;
    rItem.count = segments * 6;
    rItem.flags = static_cast< u32 >(mFlags | NEBULA_ITEM_RING | NEBULA_ITEM_INDEXED | NEBULA_ITEM_WIDE |
                                     (mState.spread_lines ? NEBULA_ITEM_SPREAD_LINES : 0) |
                                     (mState.spread_points ? NEBULA_ITEM_SPREAD_POINTS : 0));
    rItem.line_width = mState.line_width * (1.0f / NebulaFrameClock::cScreenHeight);
    rItem.point_size = mState.point_size * (1.0f / NebulaFrameClock::cScreenHeight);
}

void TypedDraw::finish() {
    if (mOpen) {
        mOpen = false;
        widen();
    }
}

SiteMeshData::SiteMeshData() : mId(0), mIndexCount(0), mStride(0), mVertexCount(0), mWritten(0), mVertices(nullptr), mIndices(nullptr) {
}

SiteMeshData::~SiteMeshData() {
    if (mId != 0) {
        const NebulaHostServices* pHost = nebula_host();
        pHost->mesh_free(pHost->user, mId);
    }
    delete[] mVertices;
    delete[] mIndices;
}

void SiteMeshData::initData(u32 stride, u32 maxVertices) {
    mStride = stride;
    mVertexCount = 0;
    mWritten = 0;
    mVertices = new u8[stride * maxVertices];
    mIndices = new u32[maxVertices * 3];
}

void SiteMeshData::initIndexedData(u32 stride, u32 vertexCount, u32 indexCount, void** ppVertices, u32** ppIndices) {
    mStride = stride;
    mVertexCount = vertexCount;
    mWritten = indexCount;
    mVertices = new u8[stride * vertexCount];
    mIndices = new u32[indexCount];
    *ppVertices = mVertices;
    *ppIndices = mIndices;
}

void* SiteMeshData::stripData(u32 count) {
    void* pOut = mVertices + mVertexCount * mStride;
    for (u32 t = 0; t + 2 < count; t++) {
        const bool odd = (t & 1) != 0;
        mIndices[mWritten++] = mVertexCount + (odd ? t + 1 : t);
        mIndices[mWritten++] = mVertexCount + (odd ? t : t + 1);
        mIndices[mWritten++] = mVertexCount + t + 2;
    }
    mVertexCount += count;
    return pOut;
}

void SiteMeshData::send() {
    NebulaFrameMesh mesh;
    mesh.vertices = mVertices;
    mesh.vertex_bytes = mVertexCount * mStride;
    mesh.stride = mStride;
    mesh.indices = mIndices;
    mesh.index_count = mWritten;
    const NebulaHostServices* pHost = nebula_host();
    mId = pHost->mesh_register(pHost->user, &mesh);
    mIndexCount = mWritten;
    delete[] mVertices;
    delete[] mIndices;
    mVertices = nullptr;
    mIndices = nullptr;
}

void SiteMeshData::drawData(TypedDraw& rDraw, const NebulaLayoutInput* pInputs, u32 inputCount, u32 flags) const {
    rDraw.drawSiteMesh(mId, mIndexCount, mStride, pInputs, inputCount, flags);
}
