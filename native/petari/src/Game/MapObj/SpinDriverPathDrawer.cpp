#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_game_api.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPositionCount_f1, "calcPositionCount.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPositionCount_f2, "calcPositionCount.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPositionList_f1, "initPositionList.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPositionList_f2, "initPositionList.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_initClipping_f1, "initClipping.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initClipping_f2, "initClipping.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPathEnd_f1, "initPathEnd.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setFadeScale_f1, "setFadeScale.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_setFadeScale_f2, "setFadeScale.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f3, "draw.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f4, "draw.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f5, "draw.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f6, "draw.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f7, "draw.f7", 0.5f);

namespace {
    // runtime/src/render/materials/spin_driver_path.hlsl: the path's colour texture (0) at its alpha times the mask's
    // (1), through texture matrices 0 and 1; added by source alpha where above zero, depth tested and not written, no
    // culling.
    SiteMaterial sPathMaterial;

    void sendPathMaterial(GameTexture* pColor, GameTexture* pMask) {
        SiteMaterial& material = sPathMaterial;
        material.init(NEBULA_SHADER_SPIN_DRIVER_PATH, SiteMaterial::BLEND_ADD_ALPHA, NEBULA_COMPARE_LEQUAL,
                           false, NEBULA_CULL_NONE);
        material.setTexture(0, pColor);
        material.setTexture(1, pMask);
    
    }
}  // namespace

SpinDriverPathDrawInit::SpinDriverPathDrawInit()
    : NameObj("スピンドライバーレール描画初期化"), mOrangeTexture(), mGreenTexture(), mPinkTexture(), mMaskTexture(), mIsPathAtOpa() {
    lumacore::setClass(this, lumacore::kClassId< SpinDriverPathDrawInit >);
    MR::registerPreDrawFunction(MR::Functor(this, &SpinDriverPathDrawInit::initDraw), MR::DrawType_SpinDriverPathDrawer);

    mOrangeTexture = new GameTexture(MR::loadTexFromArc("SpinDriverPath", "NormalColor.bti"));
    mGreenTexture = new GameTexture(MR::loadTexFromArc("SpinDriverPath", "Green.bti"));
    mPinkTexture = new GameTexture(MR::loadTexFromArc("SpinDriverPath", "Pink.bti"));
    mMaskTexture = new GameTexture(MR::loadTexFromArc("SpinDriverPath", "Mask.bti"));
}

SpinDriverPathDrawer::SpinDriverPathDrawer(SpinDriverShootPath* pShootPath)
    : LiveActor("パス描画"), mShootPath(pShootPath), mPositions(), mSides(), mUps(), mCoords(), mPositionCount(), mCoord(), mMaxCoord(), mColor(-1), mEndCut(),
      mFadeScale(1.0f), mMaskLength(5000.0f) {
    lumacore::setClass(this, lumacore::kClassId< SpinDriverPathDrawer >);
    MR::createSceneObj(SceneObj_SpinDriverPathDrawInit);
}

void SpinDriverPathDrawInit::initDraw() {
    TDDraw::cameraInit3D();

    sendPathMaterial(mOrangeTexture, mMaskTexture);
}

namespace MR {
    void setSpinDriverPathColorNormal() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mOrangeTexture, pInit->mMaskTexture);
    }

    void setSpinDriverPathColorGreen() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mGreenTexture, pInit->mMaskTexture);
    }

    void setSpinDriverPathColorPink() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mPinkTexture, pInit->mMaskTexture);
    }

    bool isDrawSpinDriverPathAtOpa() {
        if (!MR::isExistSceneObj(SceneObj_SpinDriverPathDrawInit)) {
            return false;
        }

        return MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa;
    }

    void onDrawSpinDriverPathAtOpa() {
        MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa = true;
    }

    void offDrawSpinDriverPathAtOpa() {
        MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa = false;
    }
};  // namespace MR

void SpinDriverPathDrawer::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_SpinDriverPathDrawer);

    initPositionList(cNum_init_f1, cNum_init_f2);
    initClipping();
    initPathEnd();

    makeActorDead();
}

s32 SpinDriverPathDrawer::calcPositionCount(f32 sampleStep, f32 minSideOffset) const {
    s32 totalSteps = mShootPath->getTotalLength() / sampleStep;

    Vec3f pos0;
    mShootPath->calcPosition(&pos0, 0.0f);

    Vec3f dir0;
    mShootPath->calcDirection(&dir0, 0.0f, cNum_calcPositionCount_f1);

    s32 count = 2;

    for (s32 i = 1; i < totalSteps; i++) {
        f32 posStep = static_cast< f32 >(i) / totalSteps;

        Vec3f posI;
        mShootPath->calcPosition(&posI, posStep);

        Vec3f pos;
        pos.killElement(posI - pos0, dir0);

        if (pos.length() > minSideOffset) {
            pos0 = posI;
            mShootPath->calcDirection(&dir0, posStep, cNum_calcPositionCount_f2);

            count++;
        }
    }

    return count;
}

void SpinDriverPathDrawer::initPositionList(f32 sampleStep, f32 minSideOffset) {
    mPositionCount = calcPositionCount(sampleStep, minSideOffset);
    mPositions = new Vec3f[mPositionCount];
    mSides = new Vec3f[mPositionCount];
    mUps = new Vec3f[mPositionCount];
    mCoords = new f32[mPositionCount];

    s32 totalSteps = mShootPath->getTotalLength() / sampleStep;

    Vec3f pos0;
    mShootPath->calcPosition(&pos0, 0.0f);

    Vec3f dir0;
    mShootPath->calcDirection(&dir0, 0.0f, cNum_initPositionList_f1);

    mPositions[0] = pos0;
    mCoords[0] = 0.0f;

    MR::makeAxisVerticalZX(&mUps[0], dir0);

    mSides[0].cross(mUps[0], dir0);
    MR::normalize(&mSides[0]);

    s32 count = 1;

    for (s32 i = 1; i < totalSteps; i++) {
        f32 posStep = static_cast< f32 >(i) / totalSteps;

        Vec3f posI;
        mShootPath->calcPosition(&posI, posStep);

        Vec3f pos;
        pos.killElement(posI - pos0, dir0);

        if (pos.length() > minSideOffset) {
            pos0 = posI;

            mShootPath->calcDirection(&dir0, posStep, cNum_initPositionList_f2);

            if (MR::abs(dir0.dot(mSides[count - 1])) > MR::abs(dir0.dot(mUps[count - 1]))) {
                MR::makeAxisFrontSide(&mUps[count], &mSides[count], dir0, mSides[count - 1]);
            } else {
                MR::makeAxisFrontUp(&mSides[count], &mUps[count], dir0, mUps[count - 1]);
            }

            mPositions[count] = posI;
            mCoords[count] = posStep;

            count++;
        }
    }

    mShootPath->calcPosition(&mPositions[count], 1.0f);
    mSides[count] = mSides[count - 1];
    mUps[count] = mUps[count - 1];
    mCoords[count] = 1.0f;
}

void SpinDriverPathDrawer::initClipping() {
    f32 radius = 0.0f;
    mShootPath->calcClippingInfo(&mPosition, &radius, cNum_initClipping_f1, cNum_initClipping_f2);
    MR::setClippingTypeSphere(this, radius);

    MR::validateClipping(this);
    MR::setClippingFarMax(this);
}

void SpinDriverPathDrawer::initPathEnd() {
    s32 endCutPerMille = -1;
    MR::getRailArg1WithInit(mShootPath->mRailRider, &endCutPerMille);

    if (endCutPerMille >= 0) {
        mEndCut = endCutPerMille / cNum_initPathEnd_f1;
    } else {
        mEndCut = 0.0f;
    }
}

f32 SpinDriverPathDrawer::calcDrawCode() const {
    if (mEndCut <= 0.0f) {
        return mCoord;
    }

    f32 drawEnd = 1.0f - mEndCut;
    return mCoord < drawEnd ? mCoord : drawEnd;
}

f32 SpinDriverPathDrawer::calcDrawBaseCode() const {
    if (mEndCut <= 0.0f) {
        return mMaxCoord;
    }

    f32 drawEnd = 1.0f - mEndCut;
    return mMaxCoord < drawEnd ? mMaxCoord : drawEnd;
}

void SpinDriverPathDrawer::setCoord(f32 coord) {
    mCoord = coord;

    if (mMaxCoord < coord) {
        mMaxCoord = coord;
    }
}

void SpinDriverPathDrawer::setColorNormal() {
    mColor = -1;
}

void SpinDriverPathDrawer::setColorGreen() {
    mColor = 0;
}

void SpinDriverPathDrawer::setColorPink() {
    mColor = 1;
}

void SpinDriverPathDrawer::setMaskLength(f32 mask) {
    if (mask < 1.0f) {
        mMaskLength = 1.0f;
    } else {
        mMaskLength = mask;
    }
}

void SpinDriverPathDrawer::setFadeScale(f32 fade) {
    if (fade < cNum_setFadeScale_f1) {
        mFadeScale = cNum_setFadeScale_f2;
    } else {
        mFadeScale = fade;
    }
}

void SpinDriverPathDrawer::draw() const {
    switch (mColor) {
    case -1:
        MR::setSpinDriverPathColorNormal();

        break;
    case 0:
        MR::setSpinDriverPathColorGreen();

        break;
    case 1:
        MR::setSpinDriverPathColorPink();

        break;
    default:
        MR::setSpinDriverPathColorNormal();

        break;
    }

    f32 drawCode = calcDrawCode();
    f32 drawBaseCode = calcDrawBaseCode();
    f32 maskSteps = mShootPath->getTotalLength() / mMaskLength;

    Pos3f mtx;
    mtx.identity();
    mtx[1][1] = -maskSteps;
    mtx[1][3] = cNum_draw_f1 + maskSteps * drawCode;

    sPathMaterial.setMatrix(0, 1, mtx);

    mtx.identity();
    mtx[1][1] = 1.0f / mFadeScale;
    mtx[1][3] = 1.0f - drawBaseCode * mtx[1][1];

    sPathMaterial.setMatrix(0, 0, mtx);

    TypedDraw site(sPathMaterial, nullptr, MR::getCameraViewMtx());
    for (u32 i = 1; i < mPositionCount; i++) {
        Vec3f prevPos(mPositions[i - 1]), currPos(mPositions[i]);
        Vec3f prevSide(mSides[i - 1] * cNum_draw_f2), currSide(mSides[i] * cNum_draw_f3);
        Vec3f prevUp(mUps[i - 1] * cNum_draw_f4), currUp(mUps[i] * cNum_draw_f5);

        f32 prevCoord = mCoords[i - 1];
        f32 currCoord = mCoords[i];

        // Two crossed ribbons: one across the up axis, one across the side axis.
        auto sendRibbon = [&](const Vec3f& rPrevHalf, const Vec3f& rCurrHalf) {
            VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 6);
            sendPoint(vtx, prevPos - rPrevHalf, 0.0f, prevCoord);
            sendPoint(vtx, currPos - rCurrHalf, 0.0f, currCoord);
            sendPoint(vtx, prevPos, cNum_draw_f6, prevCoord);
            sendPoint(vtx, currPos, cNum_draw_f7, currCoord);
            sendPoint(vtx, prevPos + rPrevHalf, 1.0f, prevCoord);
            sendPoint(vtx, currPos + rCurrHalf, 1.0f, currCoord);
        };
        sendRibbon(prevUp, currUp);
        sendRibbon(prevSide, currSide);
    }
}

void SpinDriverPathDrawer::sendPoint(VertexPT*& rVtx, const Vec3f& rPos, f32 u, f32 v) const {
    *rVtx++ = {rPos.x, rPos.y, rPos.z, u, v};
}
