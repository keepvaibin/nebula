#include "Game/Map/OceanBowl.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/OceanBowlBloomDrawer.hpp"
#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWaterInfo_f1, "calcWaterInfo.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadMaterial_f1, "loadMaterial.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadMaterial_f2, "loadMaterial.f2", 2.0f);

namespace {
    const s32 cPointNum = OceanBowl::cPointNum;
    const s32 cCenterPoint = cPointNum / 2;
    const f32 sPointInterval = 200.0f;
    const f32 sPointIntervalHalf = sPointInterval / 2.0f;
    const f32 sGridHalfExtent = cPointNum * sPointInterval / 2.0f;
    NEBULA_TUNABLE(f32, sPointWaveRateDistMax, 500.0f);
    NEBULA_TUNABLE(f32, sTexRate0, 0.05f);
    NEBULA_TUNABLE(f32, sTexRate1, 0.05f);
    NEBULA_TUNABLE(f32, sTexRate2, 0.1f);
    NEBULA_TUNABLE(f32, sWaveSpeed0, -0.04f);
    NEBULA_TUNABLE(f32, sWaveSpeed1, -0.04f);
    NEBULA_TUNABLE(f32, sTexSpeedU0, -0.0008f);
    NEBULA_TUNABLE(f32, sTexSpeedV0, -0.0008f);
    NEBULA_TUNABLE(f32, sTexSpeedU1, -0.001f);
    NEBULA_TUNABLE(f32, sTexSpeedV1, 0.0008f);
    NEBULA_TUNABLE(f32, sTexSpeedU2, -0.003f);
    NEBULA_TUNABLE(f32, sTexSpeedV2, -0.001f);
    NEBULA_TUNABLE(f32, sIndirectScale, 0.1f);
    NEBULA_TUNABLE(f32, sClippingDistance, 1000.0f);
    NEBULA_TUNABLE(f32, sMaskExtent, 4800.0f);  // the shore mask covers the grid's point span

    const NebulaColor8 sOceanBowlStageColor0 = {40, 40, 40, 20};
    const NebulaColor8 sOceanBowlStageColor1 = {200, 230, 210, 255};
    NEBULA_TUNABLE(u8, sOceanBowlBloomStageColor0V, 95);
    NEBULA_TUNABLE(u8, sOceanBowlBloomStageColor1V, 50);

    // One strip per row pair, two vertices per column: (u of row x + 1, v) then (u of row x, v).
    void fillStripTexCoords(f32* pArray, f32 rate) {
        s32 index = 0;
        f32 uNext = rate;
        f32 uCur = 0.0f;

        for (s32 x = 0; x < cPointNum - 1; x++) {
            f32 v = 0.0f;
            for (s32 y = 0; y < cPointNum; y++) {
                pArray[index + 0] = uNext;
                pArray[index + 1] = v;
                pArray[index + 2] = uCur;
                pArray[index + 3] = v;
                v += rate;
                index += 4;
            }

            uCur = uNext;
            uNext += rate;
        }
    }
};  // namespace

OceanBowl::OceanBowl(const char* pName)
    : LiveActor(pName), mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mRadius(), mWaveX(), mWaveZ(), mTexU0(), mTexV0(),
      mTexU1(), mTexV1(), mTexU2(), mTexV2(), mTexStep(5.0f / 120.0f), mVtxTex0Array(), mVtxTex1Array(), mVtxTex2Array(), mVtxTex3Array(), mWaterTex(),
      mWaterIndirectTex(), mMaskTex(), mBloomDrawer(), mIsClipped() {
    for (s32 x = 0; x < cPointNum; x++) {
        for (s32 y = 0; y < cPointNum; y++) {
            setPoint(x, y, nullptr);
        }
    }
}

void OceanBowl::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanBowl);

    MR::initDefaultPos(this, rIter);

    MR::calcActorAxis(&mSide, &mUp, &mFront, this);

    mRadius = cNum_init_f1 * mScale.x;
    initPoints();
    WaterAreaFunction::entryOceanBowl(this);

    mWaterTex = new GameTexture(MR::loadTexFromArc("WaterWave", "Water.bti"));
    mWaterIndirectTex = new GameTexture(MR::loadTexFromArc("WaterWave", "WaterIndirect.bti"));
    mMaskTex = new GameTexture(MR::loadTexFromArc("WaterWave", "Mask.bti"));

    mBloomDrawer = new OceanBowlBloomDrawer(this);
    mBloomDrawer->initWithoutIter();

    MR::invalidateClipping(this);

    makeActorAppeared();
}

bool OceanBowl::isInWater(const Vec3f& rPos) const {
    if (rPos.distance(mPosition) > mRadius) {
        return false;
    }

    return !(mUp.dot(rPos - mPosition) > 0.0f);
}

// The bowl is a half cosine: depth mRadius at the center, 0 at the rim.
bool OceanBowl::calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const {
    Vec3f horizontal(rPos);
    horizontal.sub(mPosition);
    f32 height = MR::vecKillElement(horizontal, mUp, &horizontal);
    // A bowl placed with no scale (radius 0) has no depth, not a division by 0.
    f32 angle = mRadius > 0.0f ? horizontal.length() / mRadius : 1.0f;
    angle *= PI;
    angle *= cNum_calcWaterInfo_f1;

    pInfo->mDepth = MR::cos(angle) * mRadius;
    pInfo->mDepth += height;

    pInfo->mCamWaterDepth = -height;
    pInfo->mWaveHeight = OceanBowlPoint::calcHeightStatic(mWaveX, mWaveZ, rPos.x, rPos.z);

    pInfo->mSurfaceNormal.set(mUp);
    pInfo->mSurfacePos.set(mPosition + horizontal);

    Vec3f toEdge(rPos);
    toEdge.sub(mPosition);

    if (toEdge.y > 0.0f) {
        toEdge.y = 0.0f;
    }

    if (MR::isNearZero(toEdge)) {
        pInfo->mEdgeDistance = mRadius;
        Vec3f edgePos(rGravity);
        edgePos.scale(mRadius);
        edgePos.add(mPosition);
        pInfo->mEdgePos.set(edgePos);
    } else {
        MR::normalize(&toEdge);
        toEdge.scale(mRadius);
        toEdge.add(mPosition);
        pInfo->mEdgeDistance = toEdge.distance(rPos);
        pInfo->mEdgePos.set(toEdge);
    }

    return true;
}

void OceanBowl::movement() {
    const Vec3f& rCenter = getPoint(cCenterPoint, cCenterPoint)->mPosition;
    f32 distToPlayer = MR::calcDistanceToPlayer(mPosition);

    if ((MR::isCameraInWater() && WaterAreaFunction::getCameraWaterInfo()->mOceanBowl != this) || distToPlayer > sClippingDistance + mRadius) {
        mIsClipped = true;
        return;
    }

    mIsClipped = false;
    Vec3f toPlayer(*MR::getPlayerPos());
    toPlayer.sub(rCenter);
    f32 sideDist = mSide.dot(toPlayer);
    f32 frontDist = mFront.dot(toPlayer);

    // The grid scrolls a row at a time to keep the player over its center.
    if (sideDist < -sPointIntervalHalf && sideDist > -mRadius) {
        moveToLeft();
    } else if (sideDist > sPointIntervalHalf && sideDist < mRadius) {
        moveToRight();
    } else if (frontDist < -sPointIntervalHalf && frontDist > -mRadius) {
        moveToUpper();
    } else if (frontDist > sPointIntervalHalf && frontDist < mRadius) {
        moveToLower();
    }

    mWaveX += sWaveSpeed0;
    mWaveZ += sWaveSpeed1;

    updatePoints();

    mTexU0 = MR::repeat(mTexU0 + sTexSpeedU0, 0.0f, 1.0f);
    mTexV0 = MR::repeat(mTexV0 + sTexSpeedV0, 0.0f, 1.0f);
    mTexU1 = MR::repeat(mTexU1 + sTexSpeedU1, 0.0f, 1.0f);
    mTexV1 = MR::repeat(mTexV1 + sTexSpeedV1, 0.0f, 1.0f);
    mTexU2 = MR::repeat(mTexU2 + sTexSpeedU2, 0.0f, 1.0f);
    mTexV2 = MR::repeat(mTexV2 + sTexSpeedV2, 0.0f, 1.0f);
}

void OceanBowl::initPoints() {
    for (s32 x = 0; x < cPointNum; x++) {
        for (s32 y = 0; y < cPointNum; y++) {
            Vec3f sideOffset(mSide);
            Vec3f frontOffset(mFront);
            sideOffset.scale(((static_cast< f32 >(y) * sPointInterval) - sGridHalfExtent) + sPointIntervalHalf);
            frontOffset.scale(((static_cast< f32 >(x) * sPointInterval) - sGridHalfExtent) + sPointIntervalHalf);
            Vec3f pos(mPosition);
            pos.add(sideOffset);
            pos.add(frontOffset);

            setPoint(x, y, new OceanBowlPoint(pos));
        }
    }

    const s32 texCoordNum = (cPointNum - 1) * cPointNum * 4;
    mVtxTex0Array = new f32[texCoordNum];
    mVtxTex1Array = new f32[texCoordNum];
    mVtxTex2Array = new f32[texCoordNum];
    mVtxTex3Array = new f32[texCoordNum];

    fillStripTexCoords(mVtxTex0Array, sTexRate0);
    fillStripTexCoords(mVtxTex1Array, sTexRate1);
    fillStripTexCoords(mVtxTex2Array, sTexRate2);
    fillStripTexCoords(mVtxTex3Array, mTexStep);
}

void OceanBowl::updatePoints() {
    for (s32 x = 0; x < cPointNum; x++) {
        for (s32 y = 0; y < cPointNum; y++) {
            getPoint(x, y)->updatePos(mWaveX, mWaveZ);
        }
    }
}

// The row that scrolled out comes back on the other side, its wave faded by its distance inside the rim.
void OceanBowl::resetWrappedPoint(OceanBowlPoint* pPoint, const Vec3f& rPos) {
    pPoint->reset(rPos, MR::clamp((mRadius - getPosition()->distance(rPos)) / sPointWaveRateDistMax, 0.0f, 1.0f));
}

void OceanBowl::moveToLeft() {
    for (s32 x = 0; x < cPointNum; x++) {
        OceanBowlPoint* pWrapped = getPoint(x, cPointNum - 1);

        for (s32 y = cPointNum - 1; y > 0; y--) {
            setPoint(x, y, getPoint(x, y - 1));
        }

        setPoint(x, 0, pWrapped);

        Vec3f pos(mSide);
        pos.scale(-sPointInterval);
        pos.add(getPoint(x, 1)->mPosition);
        resetWrappedPoint(pWrapped, pos);
    }

    mTexV0 -= sTexRate0;
    mTexV1 -= sTexRate1;
    mTexV2 -= sTexRate2;
}

void OceanBowl::moveToRight() {
    for (s32 x = 0; x < cPointNum; x++) {
        OceanBowlPoint* pWrapped = getPoint(x, 0);

        for (s32 y = 0; y < cPointNum - 1; y++) {
            setPoint(x, y, getPoint(x, y + 1));
        }

        setPoint(x, cPointNum - 1, pWrapped);

        Vec3f pos(mSide);
        pos.scale(sPointInterval);
        pos.add(getPoint(x, cPointNum - 2)->mPosition);
        resetWrappedPoint(pWrapped, pos);
    }

    mTexV0 += sTexRate0;
    mTexV1 += sTexRate1;
    mTexV2 += sTexRate2;
}

void OceanBowl::moveToUpper() {
    for (s32 y = 0; y < cPointNum; y++) {
        OceanBowlPoint* pWrapped = getPoint(cPointNum - 1, y);

        for (s32 x = cPointNum - 1; x > 0; x--) {
            setPoint(x, y, getPoint(x - 1, y));
        }

        setPoint(0, y, pWrapped);

        Vec3f pos(mFront);
        pos.scale(-sPointInterval);
        pos.add(getPoint(1, y)->mPosition);
        resetWrappedPoint(pWrapped, pos);
    }

    mTexU0 -= sTexRate0;
    mTexU1 -= sTexRate1;
    mTexU2 -= sTexRate2;
}

void OceanBowl::moveToLower() {
    for (s32 y = 0; y < cPointNum; y++) {
        OceanBowlPoint* pWrapped = getPoint(0, y);

        for (s32 x = 0; x < cPointNum - 1; x++) {
            setPoint(x, y, getPoint(x + 1, y));
        }

        setPoint(cPointNum - 1, y, pWrapped);

        Vec3f pos(mFront);
        pos.scale(sPointInterval);
        pos.add(getPoint(cPointNum - 2, y)->mPosition);
        resetWrappedPoint(pWrapped, pos);
    }

    mTexU0 += sTexRate0;
    mTexU1 += sTexRate1;
    mTexU2 += sTexRate2;
}

void OceanBowl::draw() const {
    if (mIsClipped || !MR::isValidDraw(this)) {
        return;
    }

    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    for (s32 x = 0; x < cPointNum - 1; x++) {
        VertexPCT4* vtx = site.vertices< VertexPCT4 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, cPointNum * 2);

        for (s32 y = 0; y < cPointNum; y++) {
            const OceanBowlPoint* pNext = getPoint(x + 1, y);
            const OceanBowlPoint* pCur = getPoint(x, y);
            const s32 tex = getStripTexCoordIndex(x, y);

            *vtx++ = {pNext->mVertexPosition.x, pNext->mVertexPosition.y, pNext->mVertexPosition.z, Rgba8(255, 255, 255, pNext->mAlpha),
                      mVtxTex0Array[tex], mVtxTex0Array[tex + 1], mVtxTex1Array[tex], mVtxTex1Array[tex + 1],
                      mVtxTex2Array[tex], mVtxTex2Array[tex + 1], mVtxTex3Array[tex], mVtxTex3Array[tex + 1]};

            *vtx++ = {pCur->mVertexPosition.x, pCur->mVertexPosition.y, pCur->mVertexPosition.z, Rgba8(255, 255, 255, pCur->mAlpha),
                      mVtxTex0Array[tex + 2], mVtxTex0Array[tex + 3], mVtxTex1Array[tex + 2], mVtxTex1Array[tex + 3],
                      mVtxTex2Array[tex + 2], mVtxTex2Array[tex + 3], mVtxTex3Array[tex + 2], mVtxTex3Array[tex + 3]};
        }
    }
}

SiteMaterial OceanBowl::loadMaterial() const {
    // runtime/src/render/materials/ocean_bowl.hlsl: the water (texture 0) and, above water, the screen behind (texture
    // 1) warped by the indirect texture (texture 2); alpha the vertex alpha times the shore mask (texture 3). Blended by
    // its alpha (added under water), depth tested, not written.

    const bool inWater = MR::isCameraInWater();
    GameTexture screenTexture(MR::getScreenResTIMG());
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_BOWL,
                       inWater ? SiteMaterial::BLEND_ADD_ALPHA : SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       false, NEBULA_CULL_NONE);
    material.setColor(0, sOceanBowlStageColor0.r, sOceanBowlStageColor0.g, sOceanBowlStageColor0.b,
                           sOceanBowlStageColor0.a);
    material.setColor(4, sOceanBowlStageColor1.r, sOceanBowlStageColor1.g, sOceanBowlStageColor1.b,
                           sOceanBowlStageColor1.a);
    material.constants[8] = mTexU0;
    material.constants[9] = mTexV0;
    material.constants[10] = mTexU1;
    material.constants[11] = mTexV1;
    material.constants[12] = mTexU2;
    material.constants[13] = mTexV2;
    // The shore mask follows the player: its offset from the bowl's first point, over the mask's extent.
    material.constants[14] = ((getPoint(0, 0)->mVertexPosition.z - MR::getPlayerPos()->z) + sMaskExtent / cNum_loadMaterial_f1) / sMaskExtent;
    material.constants[15] = ((getPoint(0, 0)->mVertexPosition.x - MR::getPlayerPos()->x) + sMaskExtent / cNum_loadMaterial_f2) / sMaskExtent;
    Pos3f screen;
    MR::calcTexProjectionMtx(&screen);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[16 + row * 4 + column] = screen.mMtx[row][column];
        }
    }
    material.constants[28] = sIndirectScale / screenTexture.getWidth();
    material.constants[29] = sIndirectScale / screenTexture.getHeight();
    material.constants[30] = inWater ? 0.0f : 1.0f;
    material.setTexture(0, mWaterTex);
    material.setTexture(1, &screenTexture);
    material.setTexture(2, mWaterIndirectTex);
    material.setTexture(3, mMaskTex);
    return material;
}

SiteMaterial OceanBowl::loadMaterialBloom() const {
    // runtime/src/render/materials/ocean_bowl_bloom.hlsl: the water texture at its two scrolls times the grey c0 over
    // the grey c1, alpha the texture's by the vertex alpha at both; blended by its alpha where above zero, depth
    // tested and written before texturing, no culling.

    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_BOWL_BLOOM, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    material.setColor(0, sOceanBowlBloomStageColor0V, sOceanBowlBloomStageColor0V, sOceanBowlBloomStageColor0V,
                           255);
    material.setColor(4, sOceanBowlBloomStageColor1V, sOceanBowlBloomStageColor1V, sOceanBowlBloomStageColor1V,
                           255);
    material.constants[8] = mTexU0;
    material.constants[9] = mTexV0;
    material.constants[10] = mTexU1;
    material.constants[11] = mTexV1;
    material.setTexture(0, mWaterTex);
    return material;
}
