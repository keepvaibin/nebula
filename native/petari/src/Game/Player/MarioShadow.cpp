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
#include <cstdint>
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setMode_f1, "setMode.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setMode_f2, "setMode.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setMode_f3, "setMode.f3", 18.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_create_f1, "create.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_create_f2, "create.f2", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_create_f3, "create.f3", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_create_f4, "create.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_sendPolygons_f1, "sendPolygons.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCaptureTex_f1, "initCaptureTex.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCaptureTex_f2, "initCaptureTex.f2", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_setViewMtx_f1, "setViewMtx.f1", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f1, "drawAndCaptureTex.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f2, "drawAndCaptureTex.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f3, "drawAndCaptureTex.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f4, "drawAndCaptureTex.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f5, "drawAndCaptureTex.f5", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f6, "drawAndCaptureTex.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f7, "drawAndCaptureTex.f7", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f8, "drawAndCaptureTex.f8", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f9, "drawAndCaptureTex.f9", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f10, "drawAndCaptureTex.f10", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f11, "drawAndCaptureTex.f11", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f12, "drawAndCaptureTex.f12", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f13, "drawAndCaptureTex.f13", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f14, "drawAndCaptureTex.f14", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f15, "drawAndCaptureTex.f15", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f16, "drawAndCaptureTex.f16", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f17, "drawAndCaptureTex.f17", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f18, "drawAndCaptureTex.f18", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f19, "drawAndCaptureTex.f19", 16.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f20, "drawAndCaptureTex.f20", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f21, "drawAndCaptureTex.f21", 0.385f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f22, "drawAndCaptureTex.f22", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawAndCaptureTex_f23, "drawAndCaptureTex.f23", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawVolumeBox_f1, "drawVolumeBox.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawVolumeBox_f2, "drawVolumeBox.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3_f1, "draw3.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw3_f2, "draw3.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doSortPolygons_f1, "doSortPolygons.f1", 0.33333334f);

namespace {
    f32 cCheckOffset0 = 5.0f;
    f32 cDrawOffset0 = -5.0f;

    // The shadow texture covers 300 units across (the capture's orthographic box).
    const f32 cTexScale = 1.0f / 300.0f;
    // A step in which the centre moves farther than this, or an axis turns past 60 degrees, is a jump: nothing pairs across it.
    NEBULA_TUNABLE(f32, cJumpDistance, 300.0f);
    NEBULA_TUNABLE(f32, cJumpAxisDot, 0.5f);

    // The numbers of a shadow's draws among its object's draws (Frame::setPart). A polygon takes one from its triangle in
    // [cPolygonPartFirst, cPolygonPartEnd) plus its pass's base; the other draws take fixed ones past every polygon.
    NEBULA_TUNABLE(u32, cPolygonPartFirst, 0x10000u);
    const u32 cPolygonPartEnd = 1u << 28;
    NEBULA_TUNABLE(u32, cMarkPartBase, 0u);
    const u32 cColorPartBase = 1u << 28;
    const u32 cEdgePart = 2u << 28;
    // The wipe box's first draw (CollisionShadow::draw3).
    const u32 cWipeFirstPart = 3u << 28;
    // The volume box's first draw.
    const u32 cVolumeFirstPart = 4u << 28;

    // A triangle's draw number: the same triangle (its collision part and its index in it) has the same number every step,
    // and a jump gives every triangle a new one.
    u32 polygonPart(const Triangle& rTriangle, u32 jumps) {
        u64 h = static_cast< u64 >(reinterpret_cast< uintptr_t >(rTriangle.mParts)) * 0x9E3779B97F4A7C15ull;
        h ^= (static_cast< u64 >(rTriangle.mIdx) + 1ull) * 0xC2B2AE3D27D4EB4Full;
        h ^= static_cast< u64 >(jumps) * 0x165667B19E3779F9ull;
        h ^= h >> 32;
        h *= 0xD6E8FEB86659FD93ull;
        h ^= h >> 29;
        return cPolygonPartFirst + static_cast< u32 >(h % static_cast< u64 >(cPolygonPartEnd - cPolygonPartFirst));
    }
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
        mCheckOffset = cNum_setMode_f1;
        mDrawOffset = -cNum_setMode_f2;
        break;
    case 2:
        mCheckOffset = cNum_setMode_f3;
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
    mDrawFlags = 0;
    mVolumeHeight = 0.0f;
    mVolumeWidth = 0.0f;
    mDirection.zero();
    mUp.zero();
    mVolumeUp.zero();
    mReferencePos.zero();
    mProjSide.zero();
    mProjUp.zero();
    mProjCenter.zero();
    mProjJumps = 0;
    mRadius = radius;
    mLength = length;
    mPolygonCount = 0;
    mMode = 255;
    setMode(0);
    initCaptureTex();
    mVertsPerPolygon = 3;
    makePolygonLists(128);
    mVertexCount = 0;
    mEdgePolygonCount = 0;
    mViewDone = 0;
    mUpdate = 0;
    mCaptureOn = 1;
}

void CollisionShadow::makePolygonLists(u32 num) {
    if (mPolygons != nullptr) {
        delete[] mPolygons;
        delete[] mVertices;
        delete[] mPolygonParts;
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
    mPolygonParts = new (pArena, 0) u32[num];
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
    bool success = MR::vecBlendSphere(mDirection, rDirection, &blended, cNum_create_f1);
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

    mPolygonCount = 0;
    if (!count) {
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

        if (normal.dot(MR::getCamZdir()) > cNum_create_f2) {
            continue;
        }

        if (MR::isNearZero(facing, cNum_create_f3)) {
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
    f32 tolerance = cNum_create_f4 + mDrawOffset;
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

    // The shadow texture's coordinates are made on the GPU from this projection (initMaterial), which the host blends
    // between steps; a jump gives the polygons new numbers so they do not pair across it.
    if (rPosition.squared(mProjCenter) > ::cJumpDistance * ::cJumpDistance || side.dot(mProjSide) < ::cJumpAxisDot ||
        mUp.dot(mProjUp) < ::cJumpAxisDot) {
        mProjJumps++;
    }

    mProjSide = side;
    mProjUp = mUp;
    mProjCenter = rPosition;
    for (u32 i = 0; i < mPolygonCount; i++) {
        mPolygonParts[i] = ::polygonPart(mPolygons[mPolygonIndices[i]], mProjJumps);
    }
}

// runtime/src/render/materials/color_tex_alpha.hlsl: black at the shadow texture's alpha where it passes `test` (0 above, 1 equal
// to, 2 not equal to `ref`), blended by alpha unless the site changes it; no culling. The texture's coordinate is projected
// from the vertex's world position (params.z): axes scaled to texture units and the point at the middle, in rows 2-4. Plain
// rows, so the host's blend of two steps' constants moves the whole projection (the shadow where its centre is at t).
void CollisionShadow::initMaterial(SiteMaterial* pMaterial, u32 test, u8 ref) const {
    TDDraw::setupNative(pMaterial, 1, 1, 0);
    pMaterial->setShader(NEBULA_SHADER_COLOR_TEX_ALPHA);
    pMaterial->constants[0] = 0.0f;  // the colour: black (the reference of the shader it began as is gone)
    pMaterial->constants[4] = ref;
    pMaterial->constants[5] = static_cast< f32 >(test);
    pMaterial->constants[6] = 1.0f;
    pMaterial->constants[8] = mProjSide.x * ::cTexScale;
    pMaterial->constants[9] = mProjSide.y * ::cTexScale;
    pMaterial->constants[10] = mProjSide.z * ::cTexScale;
    pMaterial->constants[12] = mProjUp.x * ::cTexScale;
    pMaterial->constants[13] = mProjUp.y * ::cTexScale;
    pMaterial->constants[14] = mProjUp.z * ::cTexScale;
    pMaterial->constants[16] = mProjCenter.x;
    pMaterial->constants[17] = mProjCenter.y;
    pMaterial->constants[18] = mProjCenter.z;
    pMaterial->setTexture(0, mCaptureTex);
}

// On a wall (a Bee's walk): marked where the shadow's texture shows, behind what is there; the screen blend darkens it.
void CollisionShadow::draw1() const {
    if (mDrawFlags & 8) {
        return;
    }

    SiteMaterial material;
    initMaterial(&material, 0, 1);
    material.setDepth(true, NEBULA_COMPARE_GEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    sendPolygons(site, ::cMarkPartBase, false, false);
}

// One draw a polygon, numbered by its triangle (partBase + mPolygonParts), so a polygon in two steps pairs with itself and the
// host blends its positions and the pass's constants; `sorted` takes the polygons in mSortOrder, `offset` moves them back
// along the direction. The vertices' texture coordinates are not read (the material projects them).
void CollisionShadow::sendPolygons(TypedDraw& rDraw, u32 partBase, bool sorted, bool offset) const {
    Vec3f shift(0.0f, 0.0f, 0.0f);
    if (offset) {
        shift = -mDirection * cNum_sendPolygons_f1;
    }

    for (u32 i = 0; i < mPolygonCount; i++) {
        rDraw.setPart(partBase + getDrawPart(i, sorted));
        VertexPT* vtx = rDraw.vertices< VertexPT >(mVertsPerPolygon == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, mVertsPerPolygon);
        for (u32 j = 0; j < mVertsPerPolygon; j++) {
            const Vec3f& point = sorted ? getDrawPos(i, j) : getDrawPos(i * mVertsPerPolygon + j);
            *vtx++ = {point.x + shift.x, point.y + shift.y, point.z + shift.z, 0.0f, 0.0f};
        }
    }
}

void CollisionShadow::initCaptureTex() {
    {
        NebulaArena::CurrentScope restorer(Arenas::scene());
        mCaptureTex = GameTexture::createTarget(cNum_initCaptureTex_f1, cNum_initCaptureTex_f2);
        MR::setMarioShadowTex(mCaptureTex);
    }

    mCaptureTex->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
}

void CollisionShadow::setViewMtx(const Vec3f& rDirection) {
    Vec3f normal = rDirection;
    MR::normalize(&normal);
    Vec3f eye = mCenter - normal * cNum_setViewMtx_f1;

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
    const f32 cEdge = 1.0f / cNum_drawAndCaptureTex_f1;

    if (!(mDrawFlags & 2)) {
        setViewMtx(mDirection);
        // The model draws with its own materials (the flat alpha pass, list 1 with 14); setup's view only.
        SiteMaterial view;
        TDDraw::setupNative(&view, 0, 0, 1);
        Mtx44 projection;
        NebulaMtxOrtho(projection, -cNum_drawAndCaptureTex_f2, cNum_drawAndCaptureTex_f3, cNum_drawAndCaptureTex_f4, -cNum_drawAndCaptureTex_f5, cNum_drawAndCaptureTex_f6, cNum_drawAndCaptureTex_f7);
        Frame::setOrthographic(projection);
        if (mCaptureOn) {
            Frame::setViewport(-cNum_drawAndCaptureTex_f8, -cNum_drawAndCaptureTex_f9, cNum_drawAndCaptureTex_f10, cNum_drawAndCaptureTex_f11, 0.0f, 1.0f);
            Frame::setScissor(cEdge, cEdge, 1.0f - cNum_drawAndCaptureTex_f12 * cEdge, 1.0f - cNum_drawAndCaptureTex_f13 * cEdge);
            pModel->setDrawView(2);
            pModel->mDrawPasses = PlayerModel::Pass_ShadowLight | PlayerModel::Pass_ShadowCapture;
            pModel->directDraw();
        }

        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(cEdge, cEdge, 1.0f - cNum_drawAndCaptureTex_f14 * cEdge, 1.0f - cNum_drawAndCaptureTex_f15 * cEdge);
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
        *vtx++ = {cNum_drawAndCaptureTex_f16, cNum_drawAndCaptureTex_f17, 0.0f, Rgba8(0xFFFFFFC0u)};
        for (u32 i = 0; i <= 16; i++) {
            const f32 angle = cNum_drawAndCaptureTex_f18 * (static_cast< f32 >(i) / cNum_drawAndCaptureTex_f19) * PI;
            *vtx++ = {cNum_drawAndCaptureTex_f20 - cNum_drawAndCaptureTex_f21 * MR::cos(angle), cNum_drawAndCaptureTex_f22 + cNum_drawAndCaptureTex_f23 * MR::sin(angle), 0.0f, Rgba8(0u)};
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
    material.setShader(NEBULA_SHADER_FLAT_COLOR);
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
    TDDraw::drawFillBox3D(site, rPosition, rDepth * cNum_drawVolumeBox_f1, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
    material.mState.cull = NEBULA_CULL_FRONT;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
    site.setMaterial(material);
    TDDraw::drawFillBox3D(site, rPosition, rDepth * cNum_drawVolumeBox_f2, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
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
    Frame::setPart(::cVolumeFirstPart);
    drawVolumeBox(position, mDirection * mLength);
    // Where the shadow's texture is clear, unmarked; and the faces in front of the volume.
    SiteMaterial material;
    initMaterial(&material, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.mState.blend_op = NEBULA_BLEND_REPLACE;
    material.mState.blend_src = NEBULA_FACTOR_ONE;
    material.mState.blend_dst = NEBULA_FACTOR_ZERO;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    sendPolygons(site, ::cMarkPartBase, true, true);
    if (mEdgePolygonCount) {
        SiteMaterial clear = material;
        clear.setShader(NEBULA_SHADER_FLAT_COLOR);
        clear.setColor(0, 0, 0, 0, 0);
        site.setMaterial(clear);
        // One draw of many triangles, which have no identity inside it: it shows its newest triangles.
        site.setFlags(NEBULA_ITEM_DISCRETE_VERTICES);
        site.setPart(::cEdgePart);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLES, mEdgePolygonCount * 3);
        for (u32 i = 0; i < mEdgePolygonCount; i++) {
            for (u32 j = 0; j < 3; j++) {
                const Vec3f& point = *mPolygons[mEdgePolygons[i]].getPos(j);
                *vtx++ = {point.x, point.y, point.z, -1.0f, -1.0f};
            }
        }

        site.setFlags(0);
    }

    // The shadow's colour at its texture's alpha where the mark stands, nearest polygon first; the mark goes as it is drawn.
    material.constants[5] = cNum_draw3_f1;
    material.mState.color_write = 1;
    material.setDepth(false, NEBULA_COMPARE_ALWAYS, false);
    material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
    site.setMaterial(material);
    sendPolygons(site, ::cColorPartBase, true, false);

    // What the polygons did not cover.
    SiteMaterial wipe = material;
    wipe.setShader(NEBULA_SHADER_FLAT_COLOR);
    wipe.mState.color_write = 0;
    wipe.mState.blend_op = NEBULA_BLEND_REPLACE;
    wipe.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    site.setMaterial(wipe);
    // The wipe box is the volume box again and must cover the pixels it marked: it pairs with itself and blends as the
    // volume box does, so on a generated frame both stand where the polygons' projection puts the shadow.
    site.setPart(::cWipeFirstPart);
    Vec3f up(mVolumeUp);
    Vec3f side;
    side.cross(mDirection, up);
    MR::normalizeOrZero(&side);
    TDDraw::drawFillBox3D(site, position, mDirection * mLength * cNum_draw3_f2, side * mVolumeWidth, up * mVolumeHeight, 0xFFFFFF01);
}

void CollisionShadow::doSortPolygons() {
    f32* distances = mSortDistances;
    f32 inverseVertexCount = cNum_doSortPolygons_f1;
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

u32 CollisionShadow::getDrawPart(u32 polygon, bool sorted) const {
    return mPolygonParts[sorted ? mSortOrder[polygon] : polygon];
}
