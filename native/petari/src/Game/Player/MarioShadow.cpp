#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include <cstring>

namespace {
    f32 cCheckOffset0 = 5.0f;
    f32 cDrawOffset0 = -5.0f;
};  // namespace

CollisionShadow::~CollisionShadow() {
}

void CollisionShadow::setMode(u32 mode) {
    if (mMode == mode) {
        return;
    }

    switch (mode) {
    case 0:
        mCheckOffset = ::cCheckOffset0;
        mDrawOffset = ::cDrawOffset0;
        break;
    case 1:
        mCheckOffset = 50.0f;
        mDrawOffset = -150.0f;
        break;
    case 2:
        mCheckOffset = 18.0f;
        mDrawOffset = 0.0f;
        break;
    }

    mMode = mode;
}

CollisionShadow::CollisionShadow(f32 radius, f32 length) : NameObj("投影シャドウ") {
    mCenter.zero();
    mCaptureTex = nullptr;
    mSnapDirection = false;
    mCheckOffset = 0.0f;
    mDrawOffset = 0.0f;
    mPolygons = nullptr;
    mFoundCount = 0;
    mDrawFlags = 0;
    mMaxRadius = radius;
    mVolumeHeight = 0.0f;
    mVolumeWidth = 0.0f;
    mDirection.zero();
    mUp.zero();
    mVolumeUp.zero();
    mReferencePos.zero();
    mRadius = radius;
    mLength = length;
    mPolygonCount = 0;
    mMode = 255;
    setMode(0);
    initCaptureTex();
    mVertsPerPolygon = 3;
    makePolygonLists(128);
    mVertexCount = 0;
    mFoundCount = 0;
    mEdgePolygonCount = 0;
    mViewDone = 0;
    mUpdate = 0;
    mCaptureOn = 1;
}

void CollisionShadow::makePolygonLists(u32 num) {
    if (mPolygons != nullptr) {
        delete[] mPolygons;
        delete[] mVertices;
        delete[] mTexCoords;
        delete[] mVertexIndices;
        delete[] mPolygonIndices;
        delete[] mEdgePolygons;
        delete[] mSortOrder;
        delete[] mSortDistances;
    }

    NebulaArena* pArena = NebulaArena::findFromRoot(this);
    mListCapacity = num;
    mPolygons = new (pArena, 0) Triangle[num];
    mVertices = new (pArena, 0) Vec3f[num * mVertsPerPolygon];
    mTexCoords = new (pArena, 0) Vec2f[num * mVertsPerPolygon];
    mVertexIndices = new (pArena, 0) u32[num * mVertsPerPolygon];
    mPolygonIndices = new (pArena, 0) u32[num];
    mEdgePolygons = new (pArena, 0) u32[num]();
    mSortOrder = new (pArena, 0) s32[num]();
    mSortDistances = new (pArena, 0) f32[num];
}

void CollisionShadow::create(const Vec3f& rPosition, const Vec3f& rDirection, const Vec3f& rUp) {
    Vec3f corners[8];
    Vec3f blended;
    Vec3f vertical(rUp);
    bool success = MR::vecBlendSphere(mDirection, rDirection, &blended, 0.1f);
    if (MR::isNearZero(mDirection) || !success || mSnapDirection) {
        blended = rDirection;
    }

    mSnapDirection = false;
    if (!MR::isNearZero(blended)) {
        mDirection = blended;
        MR::vecKillElement(vertical, blended, &vertical);
        MR::normalizeOrZero(&vertical);
        if (!MR::isNearZero(vertical)) {
            mUp = vertical;
        }
    }

    mCenter = rPosition;
    if (mMode == 3) {
        return;
    }

    Vec3f side;
    side.cross(vertical, blended);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    for (u32 i = 0; i < 8; i++) {
        corners[i] = rPosition;
        if (i & 1) {
            corners[i] += vertical * mRadius;
        } else {
            corners[i] -= vertical * mRadius;
        }

        if (i & 2) {
            corners[i] += side * mRadius;
        } else {
            corners[i] -= side * mRadius;
        }

        if (i & 4) {
            corners[i] -= blended * mCheckOffset;
        } else {
            corners[i] += blended * mLength;
        }
    }

    Vec3f minimum;
    Vec3f maximum;
    MR::createBoundingBox(corners, 8, &minimum, &maximum);
    if (MR::isInitializeStatePlacementSomething()) {
        return;
    }

    u32 count = MR::createAreaPolygonListArray(mPolygons, mListCapacity, corners, 8);
    while (count >= mListCapacity) {
        makePolygonLists(mListCapacity * 2);
        count = MR::createAreaPolygonListArray(mPolygons, mListCapacity, corners, 8);
    }

    mFoundCount = count;
    mPolygonCount = 0;
    if (!count) {
        mRadius = mMaxRadius;
        return;
    }

    u32 polygonCount = 0;
    mEdgePolygonCount = 0;
    for (u32 i = 0; i < count; i++) {
        Vec3f normal(*mPolygons[i].getNormal(0));
        f32 facing = normal.dot(mDirection);
        if (facing > 0.0f) {
            continue;
        }

        if (normal.dot(MR::getCamZdir()) > 0.707f) {
            continue;
        }

        if (MR::isNearZero(facing, 0.15f)) {
            mEdgePolygons[mEdgePolygonCount] = i;
            mEdgePolygonCount++;
        } else {
            const s32 code = MR::getFloorCodeIndex(&mPolygons[i]);
            if (code != CollisionFloorCode_PullBack && code != CollisionFloorCode_Glass) {
                mPolygonIndices[polygonCount] = i;
                polygonCount++;
            }
        }
    }

    mPolygonCount = polygonCount;
    f32 tolerance = 2.0f + mDrawOffset;
    mVertexCount = 0;
    s32 index = 0;
    for (u32 i = 0; i < mPolygonCount; i++) {
        for (u32 j = 0; j < mVertsPerPolygon; j++) {
            const Vec3f& point = *mPolygons[mPolygonIndices[i]].getPos(j);
            u32 k;
            for (k = 0; k < mVertexCount; k++) {
                if (MR::abs(mVertices[k].x - point.x) < tolerance && MR::abs(mVertices[k].y - point.y) < tolerance && MR::abs(mVertices[k].z - point.z) < tolerance) {
                    break;
                }
            }

            if (k == mVertexCount) {
                const Vec3f& normal = *mPolygons[mPolygonIndices[i]].getNormal(0);
                mVertices[mVertexCount] = point + normal * mDrawOffset;
                mVertexCount++;
            }

            mVertexIndices[index] = k;
            index++;
        }
    }

    const f32 texScale = 1.0f / 300.0f;

    for (u32 i = 0; i < mVertexCount; i++) {
        Vec3f relative(mVertices[i] - rPosition);
        f32 s = relative.dot(side);
        f32 t = relative.dot(mUp);

        mTexCoords[i].x = 0.5f + s * texScale;
        mTexCoords[i].y = 0.5f + t * texScale;
    }

    if (mFoundCount > 40) {
        mRadius -= 10.0f;
    } else if (mFoundCount > 10) {
        mRadius -= 1.0f;
    } else if (mFoundCount > 20) {
        mRadius -= 4.0f;
    } else {
        mRadius += 1.0f;
    }

    MR::clampBoth(&mRadius, 1.0f, mMaxRadius);
}

namespace {
    // runtime/src/render/materials/color_tex_alpha.hlsl: black at the shadow texture's alpha where it passes `test`
    // (0 above, 1 equal to, 2 not equal to `ref`), blended by alpha unless the site changes it; no culling.
    void initShadowMaterial(SiteMaterial* pMaterial, const GameTexture* pTexture, u32 test, u8 ref) {
        TDDraw::setupNative(pMaterial, 1, 1, 0);
        pMaterial->mShader = NEBULA_SHADER_COLOR_TEX_ALPHA;
        pMaterial->constants[4] = ref;
        pMaterial->constants[5] = static_cast< f32 >(test);
        pMaterial->setTexture(0, pTexture);
    }
}  // namespace

// On a wall (a Bee's walk): marked where the shadow's texture shows, behind what is there; the screen blend darkens it.
void CollisionShadow::draw1() const {
    if (mDrawFlags & 8) {
        return;
    }

    SiteMaterial material;
    initShadowMaterial(&material, mCaptureTex, 0, 1);
    material.setDepth(true, NEBULA_COMPARE_GEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    // Rebuilt collision triangles can change order even with the same vertex count.
    site.setFlags(NEBULA_ITEM_DISCRETE_VERTICES);
    sendVtx(site);
}

void CollisionShadow::sendVtx(TypedDraw& rDraw) const {
    if (!mPolygonCount) {
        return;
    }

    u32 vertexCount = mPolygonCount * mVertsPerPolygon;
    VertexPT* vtx = rDraw.vertices< VertexPT >(mVertsPerPolygon == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, vertexCount);

    const Vec2f* pTex;
    u32 index = 0;
    for (u32 i = 0; i < mPolygonCount; i++) {
        for (u32 j = 0; j < mVertsPerPolygon; j++) {
            const Vec3f& point = getDrawPos(index);
            pTex = &mTexCoords[mVertexIndices[index]];
            index++;
            *vtx++ = {point.x, point.y, point.z, pTex->x, pTex->y};
        }
    }
}

void CollisionShadow::sendZsortedVtx(TypedDraw& rDraw, bool offset) const {
    u32 vertexCount = mPolygonCount * mVertsPerPolygon;
    VertexPT* vtx = rDraw.vertices< VertexPT >(mVertsPerPolygon == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, vertexCount);

    Vec3f shift(-mDirection * 5.0f);
    for (u32 i = 0; i < mPolygonCount; i++) {
        for (u32 j = 0; j < mVertsPerPolygon; j++) {
            const Vec3f& point = getDrawPos(i, j);
            const Vec2f& tex = getDrawTx(i, j);
            if (offset) {
                *vtx++ = {point.x + shift.x, point.y + shift.y, point.z + shift.z, tex.x, tex.y};
            } else {
                *vtx++ = {point.x, point.y, point.z, tex.x, tex.y};
            }
        }
    }
}

void CollisionShadow::initCaptureTex() {
    {
        NebulaArena::CurrentScope restorer(Arenas::scene());
        mCaptureTex = GameTexture::createTarget(0.25f, 0.35f);
        MR::setMarioShadowTex(mCaptureTex);
    }

    mCaptureTex->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
}

void CollisionShadow::setViewMtx(const Vec3f& rDirection) {
    Vec3f normal = rDirection;
    MR::normalize(&normal);
    Vec3f eye = mCenter - normal * 10000.0f;

    Pos3f matrix;
    matrix.identity();
    matrix.setPositionFromLookAt(eye, mUp, mCenter);
    NebulaMtxCopy(matrix, ModelSys::mViewMtx);
    ModelSys::mViewRole = NEBULA_CAMERA_SHADOW;
    MR::setMarioShadowVec(rDirection);
}

void CollisionShadow::setUpdateFlag() {
    if (!MR::isNearZero(mDirection)) {
        mUpdate = 1;
    }
}

void CollisionShadow::calcView(PlayerModel* pModel, u32 view, PlayerModel* pReference) {
    if (!mUpdate) {
        return;
    }

    const Vec3f& rDirection = mDirection;
    mUpdate = 0;
    mViewDone = 1;
    if (mMode == 3) {
        Mtx matrix;
        NebulaMtxCopy(pReference->getBaseTRMtx(), matrix);
        if (pReference != nullptr) {
            pModel->viewCalcRefPos(view, pReference, mReferencePos, mDirection);
        }
    } else {
        setViewMtx(rDirection);
        if (pReference != nullptr) {
            pModel->viewCalcRef(view, pReference);
        } else {
            pModel->viewCalc3(view, nullptr);
        }
    }

    if (mMode == 2) {
        doSortPolygons();
    }
}

void CollisionShadow::drawAndCaptureTex(PlayerModel* pModel, const Vec3f& rPosition) {
    if (!mViewDone) {
        return;
    }

    // Mario's silhouette, a pass into the shadow texture.
    const ResTIMG* image = mCaptureTex->info();
    const void* destination = image->image();
    Frame::targetBegin(destination, mCaptureTex->shareAcross(), mCaptureTex->shareDown(), 0);
    // A pixel of the picture's edge stays clear for the texture's clamp.
    const f32 cEdge = 1.0f / 160.0f;

    if (!(mDrawFlags & 2)) {
        setViewMtx(mDirection);
        // The model draws with its own materials (the flat alpha pass, list 1 with 14); setup's view only.
        SiteMaterial view;
        TDDraw::setupNative(&view, 0, 0, 1);
        Mtx44 projection;
        NebulaMtxOrtho(projection, -150.0f, 150.0f, 150.0f, -150.0f, 10.0f, 100000.0f);
        Frame::setOrthographic(projection);
        if (mCaptureOn) {
            Frame::setViewport(-0.2f, -0.2f, 1.4f, 1.4f, 0.0f, 1.0f);
            Frame::setScissor(cEdge, cEdge, 1.0f - 2.0f * cEdge, 1.0f - 2.0f * cEdge);
            pModel->setDrawView(2);
            pModel->mDrawPasses = PlayerModel::Pass_ShadowLight | PlayerModel::Pass_ShadowCapture;
            pModel->directDraw();
        }

        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(cEdge, cEdge, 1.0f - 2.0f * cEdge, 1.0f - 2.0f * cEdge);
        pModel->setDrawView(2);
        pModel->mDrawPasses = PlayerModel::Pass_ShadowCapture;
        pModel->directDraw();
        pModel->mDrawPasses &= ~PlayerModel::Pass_ShadowCapture;
    }

    if (!(mDrawFlags & 4)) {
        // runtime/src/render/materials/vertex_color.hlsl into the alpha: the shadow faded to the edge (its alpha times the circle's).
        SiteMaterial circle;
        TDDraw::setupNative(&circle, 0, 1, 2);
        ImageEffectLocalUtil::setupDrawTexture();
        circle.mState.color_write = 0;
        circle.mState.alpha_write = 1;
        circle.setBlend(NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ZERO);
        TypedDraw site = TypedDraw::eyeSpace(circle, cIdentityMtx);
    VertexPC* vtx = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, 18);
        *vtx++ = {0.5f, 0.5f, 0.0f, Rgba8(0xFFFFFFC0u)};
        for (u32 i = 0; i <= 16; i++) {
            const f32 angle = 2.0f * (static_cast< f32 >(i) / 16.0f) * PI;
            *vtx++ = {0.5f - 0.385f * MR::cos(angle), 0.5f + 0.5f * MR::sin(angle), 0.0f, Rgba8(0u)};
        }
    }

    Frame::targetEnd(destination, NEBULA_TARGET_ALPHA);

    MR::setDefaultViewportAndScissor();
    TDDraw::cameraInit3D();
}

void CollisionShadow::drawVolumeBox(const Vec3f& rPosition, const Vec3f& rDepth) const {
    Vec3f up(mVolumeUp);
    Vec3f side;
    side.cross(mDirection, up);
    MR::normalizeOrZero(&side);
    // The box's faces in the stencil, depth tested: what its front faces reach and its back faces do not.
    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.mShader = NEBULA_SHADER_FLAT_COLOR;
    material.setColor(0, 255, 255, 255, 255);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.mState.blend_op = NEBULA_BLEND_REPLACE;
    material.mState.blend_src = NEBULA_FACTOR_ONE;
    material.mState.blend_dst = NEBULA_FACTOR_ZERO;
    material.mState.cull = NEBULA_CULL_BACK;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    TDDraw::drawFillBox3D(site, rPosition, rDepth * 2.0f, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
    material.mState.cull = NEBULA_CULL_FRONT;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
    site.setMaterial(material);
    TDDraw::drawFillBox3D(site, rPosition, rDepth * 2.0f, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
}

void CollisionShadow::draw() const {
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    if (mMode == 0) {
        draw1();
    } else {
        draw3();
    }
}

// On the ground: the shadow's polygons inside its box, each pixel darkened once.
void CollisionShadow::draw3() const {
    if (mDrawFlags & 8) {
        return;
    }

    Vec3f position(mCenter - mDirection * mCheckOffset);
    drawVolumeBox(position, mDirection * mLength);
    // Where the shadow's texture is clear, unmarked; and the faces in front of the volume.
    SiteMaterial material;
    initShadowMaterial(&material, mCaptureTex, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.mState.blend_op = NEBULA_BLEND_REPLACE;
    material.mState.blend_src = NEBULA_FACTOR_ONE;
    material.mState.blend_dst = NEBULA_FACTOR_ZERO;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    // Rebuilt collision triangles can change order even with the same vertex count.
    site.setFlags(NEBULA_ITEM_DISCRETE_VERTICES);
    sendZsortedVtx(site, true);
    if (mEdgePolygonCount) {
        SiteMaterial clear = material;
        clear.mShader = NEBULA_SHADER_FLAT_COLOR;
        clear.setColor(0, 0, 0, 0, 0);
        site.setMaterial(clear);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLES, mEdgePolygonCount * 3);
        for (u32 i = 0; i < mEdgePolygonCount; i++) {
            for (u32 j = 0; j < 3; j++) {
                const Vec3f& point = *mPolygons[mEdgePolygons[i]].getPos(j);
                *vtx++ = {point.x, point.y, point.z, -1.0f, -1.0f};
            }
        }
    }

    // The shadow's colour at its texture's alpha where the mark stands, nearest polygon first; the mark goes as it is drawn.
    material.constants[5] = 2.0f;
    material.mState.color_write = 1;
    material.setDepth(false, NEBULA_COMPARE_ALWAYS, false);
    material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
    site.setMaterial(material);
    for (u32 i = 0; i < mPolygonCount; i++) {
        VertexPT* vtx = site.vertices< VertexPT >(mVertsPerPolygon == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, mVertsPerPolygon);

        for (u32 j = 0; j < mVertsPerPolygon; j++) {
            const Vec3f& point = getDrawPos(i, j);
            const Vec2f& tex = getDrawTx(i, j);
            *vtx++ = {point.x, point.y, point.z, tex.x, tex.y};
        }
    }

    // What the polygons did not cover.
    SiteMaterial wipe = material;
    wipe.mShader = NEBULA_SHADER_FLAT_COLOR;
    wipe.mState.color_write = 0;
    wipe.mState.blend_op = NEBULA_BLEND_REPLACE;
    wipe.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    site.setMaterial(wipe);
    Vec3f up(mVolumeUp);
    Vec3f side;
    side.cross(mDirection, up);
    MR::normalizeOrZero(&side);
    TDDraw::drawFillBox3D(site, position, mDirection * mLength * 2.0f, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
}

void CollisionShadow::doSortPolygons() {
    f32* distances = mSortDistances;
    f32 inverseVertexCount = 0.33333334f;
    for (u32 i = 0; i < mPolygonCount; i++) {
        Vec3f center(getDrawPos(0));
        center += getDrawPos(1);
        center += getDrawPos(2);
        center.scale(inverseVertexCount);
        Vec3f relative(center);
        relative.sub(mCenter);
        distances[i] = relative.dot(mDirection);
    }

    MR::sortSmall(mPolygonCount, distances, mSortOrder);
}

const Vec3f& CollisionShadow::getDrawPos(u32 index) const {
    return mVertices[mVertexIndices[index]];
}

const Vec3f& CollisionShadow::getDrawPos(u32 polygon, u32 vertex) const {
    return mVertices[mVertexIndices[vertex + mVertsPerPolygon * mSortOrder[polygon]]];
}

const Vec2f& CollisionShadow::getDrawTx(u32 polygon, u32 vertex) const {
    return mTexCoords[mVertexIndices[vertex + mVertsPerPolygon * mSortOrder[polygon]]];
}
