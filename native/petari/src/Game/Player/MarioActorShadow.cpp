#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f1, "calcViewReflectionModel.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f2, "calcViewReflectionModel.f2", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f3, "calcViewReflectionModel.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f4, "calcViewReflectionModel.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f5, "calcViewReflectionModel.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f6, "calcViewReflectionModel.f6", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f7, "calcViewReflectionModel.f7", 1.414f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f8, "calcViewReflectionModel.f8", 64.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f9, "calcViewReflectionModel.f9", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f10, "calcViewReflectionModel.f10", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f11, "calcViewReflectionModel.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f12, "calcViewReflectionModel.f12", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f13, "calcViewReflectionModel.f13", 1.414f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewReflectionModel_f14, "calcViewReflectionModel.f14", 127.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f1, "calcViewWallShadowModel.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f2, "calcViewWallShadowModel.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f3, "calcViewWallShadowModel.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f4, "calcViewWallShadowModel.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f5, "calcViewWallShadowModel.f5", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f6, "calcViewWallShadowModel.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewWallShadowModel_f7, "calcViewWallShadowModel.f7", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideShadowMode_f1, "decideShadowMode.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideShadowMode_f2, "decideShadowMode.f2", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideShadowMode_f3, "decideShadowMode.f3", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideShadowMode_f4, "decideShadowMode.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCylinderToCenter_f1, "calcCylinderToCenter.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCylinderToCenter_f2, "calcCylinderToCenter.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCylinderToCenter_f3, "calcCylinderToCenter.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCylinderToCenter_f4, "calcCylinderToCenter.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcViewFootPrint_f1, "calcViewFootPrint.f1", 0.1f);

void MarioActor::initShadow() {
    mShadowMode = 1;

    if (gIsLuigi) {
        mSimpleModelHolder = new ModelHolder("LuigiShadow", true);
    } else {
        mSimpleModelHolder = new ModelHolder("MarioShadow", true);
    }

    mSimpleModelHolder->initWithoutIter();

    mSimpleModel = static_cast< PlayerModel* >(MR::getModel(mSimpleModelHolder));
    PlayerModel* pSource = mModels[0];
    PlayerModel* pShadow = mSimpleModel;
    pShadow->copyExtraPoses(pSource);
}

void MarioActor::calcViewReflectionModel() {
    PlayerModel* pModel = getModel();
    Pos3f matrix;
    NebulaMtxCopy(getModel()->getBaseTRMtx(), matrix.toMtxPtr());
    Pos3f offset;
    NebulaMtxIdentity(offset.toMtxPtr());
    f32 factor;
    f32 distance = cNum_calcViewReflectionModel_f1;
    if (isAnimationRun("ターンジャンプ")) {
        distance = cNum_calcViewReflectionModel_f2;
    }

    if (mShadowMode != 6 && mShadowMode != 7) {
        mReflectSimpleAlpha = 0;
    }

    if (mShadowMode != 3) {
        mWaterReflectAlpha = 0;
    }

    Vec3f position;
    switch (mShadowMode) {
    case 2: {
        matrix.mMtx[0][1] = -matrix.mMtx[0][1];
        matrix.mMtx[1][1] = -matrix.mMtx[1][1];
        matrix.mMtx[2][1] = -matrix.mMtx[2][1];
        position = mMario->mShadowPos - *mMario->mShadowPolygon->getNormal(0) * distance;
        Vec3f root;
        getRealPos("All_Root", &root);
        Vec3f translation(mPosition - root);
        NebulaMtxTrans(offset.toMtxPtr(), translation.x, translation.y, translation.z);
        updateReflectAlphaList(mReflectAlpha);
        break;
    }
    case 3: {
        Vec3f x;
        x.set< f32 >(matrix.mMtx[0][0], matrix.mMtx[1][0], matrix.mMtx[2][0]);
        Vec3f y;
        y.set< f32 >(matrix.mMtx[0][1], matrix.mMtx[1][1], matrix.mMtx[2][1]);
        Vec3f z;
        z.set< f32 >(matrix.mMtx[0][2], matrix.mMtx[1][2], matrix.mMtx[2][2]);
        const Vec3f& rNormal = mMario->mSwim->mSurfacePos;
        f32 dx = MR::vecKillElement(x, rNormal, &x);
        f32 dy = MR::vecKillElement(y, rNormal, &y);
        f32 dz = MR::vecKillElement(z, rNormal, &z);
        x -= rNormal * dx;
        y -= rNormal * dy;
        z -= rNormal * dz;
        matrix.mMtx[0][0] = x.x;
        matrix.mMtx[1][0] = x.y;
        matrix.mMtx[2][0] = x.z;
        matrix.mMtx[0][1] = y.x;
        matrix.mMtx[1][1] = y.y;
        matrix.mMtx[2][1] = y.z;
        matrix.mMtx[0][2] = z.x;
        matrix.mMtx[1][2] = z.y;
        matrix.mMtx[2][2] = z.z;
        position = mMario->mSwim->mSurfaceNorm - rNormal * distance;
        f32 depth = -mMario->mSwim->mWaterDepth;
        if (depth < cNum_calcViewReflectionModel_f3) {
            factor = 0.0f;
        } else if (depth < cNum_calcViewReflectionModel_f4) {
            factor = (depth - cNum_calcViewReflectionModel_f5) / cNum_calcViewReflectionModel_f6;
        } else {
            factor = 1.0f;
        }

        f32 alpha = mCamDirZ.dot(rNormal);
        MR::clamp01(&alpha);
        alpha *= cNum_calcViewReflectionModel_f7;
        MR::clamp01(&alpha);
        alpha *= cNum_calcViewReflectionModel_f8;
        f32 target = factor * alpha;
        if (mWaterReflectAlpha > static_cast< u8 >((s32)(target))) {
            mWaterReflectAlpha--;
        } else if (mWaterReflectAlpha < static_cast< u8 >((s32)(target))) {
            mWaterReflectAlpha++;
        }

        updateSimpleAlphaList(mWaterReflectAlpha);
        mReflectPos = position;
        break;
    }
    case 6:
    case 7: {
        Vec3f x;
        x.set< f32 >(matrix.mMtx[0][0], matrix.mMtx[1][0], matrix.mMtx[2][0]);
        Vec3f y;
        y.set< f32 >(matrix.mMtx[0][1], matrix.mMtx[1][1], matrix.mMtx[2][1]);
        Vec3f z;
        z.set< f32 >(matrix.mMtx[0][2], matrix.mMtx[1][2], matrix.mMtx[2][2]);
        Vec3f normal(mMario->mSwim->mWaterEdgePos - mPosition);
        MR::normalize(&normal);
        f32 dx = MR::vecKillElement(x, normal, &x);
        f32 dy = MR::vecKillElement(y, normal, &y);
        f32 dz = MR::vecKillElement(z, normal, &z);
        x -= normal * dx;
        y -= normal * dy;
        z -= normal * dz;
        matrix.mMtx[0][0] = x.x;
        matrix.mMtx[1][0] = x.y;
        matrix.mMtx[2][0] = x.z;
        matrix.mMtx[0][1] = y.x;
        matrix.mMtx[1][1] = y.y;
        matrix.mMtx[2][1] = y.z;
        matrix.mMtx[0][2] = z.x;
        matrix.mMtx[1][2] = z.y;
        matrix.mMtx[2][2] = z.z;
        position = mMario->mSwim->mWaterEdgePos - normal * distance;
        f32 edgeDistance = mMario->mSwim->getWaterEdgeDist();
        if (edgeDistance < 0.0f) {
            factor = 0.0f;
        } else if (edgeDistance < cNum_calcViewReflectionModel_f9) {
            factor = cNum_calcViewReflectionModel_f10 + cNum_calcViewReflectionModel_f11 * (edgeDistance / cNum_calcViewReflectionModel_f12);
        } else {
            factor = 0.0f;
        }

        f32 alpha = mCamDirZ.dot(normal);
        MR::clamp01(&alpha);
        alpha *= cNum_calcViewReflectionModel_f13;
        MR::clamp01(&alpha);
        alpha *= cNum_calcViewReflectionModel_f14;
        f32 target = factor * alpha;
        if (mReflectSimpleAlpha > static_cast< u8 >((s32)(target))) {
            mReflectSimpleAlpha--;
        } else if (mReflectSimpleAlpha < static_cast< u8 >((s32)(target))) {
            mReflectSimpleAlpha++;
        }

        updateSimpleAlphaList(mReflectSimpleAlpha);
        mReflectPos = position;
        break;
    }
    }

    matrix.setTrans(position);
    NebulaMtxCopy(matrix.toMtxPtr(), mTurtleShadowMtx.toMtxPtr());
    if (getCarrySensor() != nullptr) {
        Pos3f carry;
        NebulaMtxConcat(mModelUnbaseMtx.mMtx, MR::getJointMtx(getCarrySensor()->mHost, 0), carry.toMtxPtr());
        NebulaMtxConcat(mTurtleShadowMtx.toMtxPtr(), carry.toMtxPtr(), mTurtleShadowMtx.toMtxPtr());
        mTurtleShadow->calcType0(mTurtleShadowMtx.toMtxPtr());
    }

    NebulaMtxConcat(mModelUnbaseMtx.mMtx, offset.toMtxPtr(), offset.toMtxPtr());
    NebulaMtxConcat(matrix.toMtxPtr(), offset.toMtxPtr(), matrix.toMtxPtr());
    NebulaMtxConcat(ModelSys::mViewMtx, matrix.toMtxPtr(), ModelSys::mViewMtx);
    pModel->viewCalc3(3, nullptr);
    MR::loadViewMtx();
}

void MarioActor::calcViewWallShadowModel() {
    mWallShadeOn = 0;
    if (mShadowMode & 2) {
        return;
    }

    if (mBeeWallWalk) {
        return;
    }

    Vec3f direction;
    s32 length = 0;
    const AreaObj* pArea = MR::getAreaObj("ExtraWallCheckArea", mCenterPos);
    if (pArea != nullptr) {
        MR::calcCubeAxisZ(pArea, &direction);
        length = MR::getAreaObjArg(pArea, 0);
        mWallShadeArea = pArea;
        mWallShadeAreaKind = 0;
    } else {
        pArea = MR::getAreaObj("ExtraWallCheckCylinder", mCenterPos);
        if (pArea != nullptr) {
            length = calcCylinderToCenter(pArea, &direction);
            mWallShadeArea = pArea;
            mWallShadeAreaKind = 1;
        }
    }

    bool fading = false;
    if (pArea == nullptr || static_cast< f32 >(length) < 0.0f) {
        if (mWallShadeDist >= cNum_calcViewWallShadowModel_f1) {
            return;
        }

        mWallShadeDist += cNum_calcViewWallShadowModel_f2;
        if (mWallShadeDist >= cNum_calcViewWallShadowModel_f3) {
            mWallShadeDist = cNum_calcViewWallShadowModel_f4;
        }

        switch (mWallShadeAreaKind) {
        case 0:
            MR::calcCubeAxisZ(mWallShadeArea, &direction);
            length = MR::getAreaObjArg(mWallShadeArea, 0);
            break;
        case 1:
            length = calcCylinderToCenter(mWallShadeArea, &direction);
            break;
        }

        fading = true;
    }

    Triangle triangle;
    Vec3f position;
    if (!MR::getFirstPolyOnLineToMap(&position, &triangle, mCenterPos, direction * static_cast< f32 >(length))) {
        return;
    }

    mWallShadePos = position;
    mWallShadeNormal = *triangle.getNormal(0);
    if (!fading) {
        mWallShadeDist = cNum_calcViewWallShadowModel_f5 * mWallShadeDist + cNum_calcViewWallShadowModel_f6 * (position - mCenterPos).length();
    }

    updateRandomTexture(mWallShadeDist);
    Vec3f normal(direction);
    MR::normalizeOrZero(&normal);
    PlayerModel* pModel = getModel();
    PlayerModel* pSimple = getSimpleModel();
    NebulaMtxCopy(pModel->getBaseTRMtx(), pSimple->getBaseTRMtx());
    MtxPtr matrix = pSimple->getBaseTRMtx();
    Vec3f x;
    MR::extractMtxXDir(matrix, &x);
    MR::vecKillElement(x, normal, &x);
    Vec3f y;
    MR::extractMtxYDir(matrix, &y);
    MR::vecKillElement(y, normal, &y);
    Vec3f z;
    MR::extractMtxZDir(matrix, &z);
    MR::vecKillElement(z, normal, &z);
    MR::setMtxAxisXYZ(matrix, x, y, z);
    Vec3f horizontal;
    MR::vecKillElement(mPosition - position, getGravityVec(), &horizontal);
    Vec3f translation = mPosition + normal * (cNum_calcViewWallShadowModel_f7 + horizontal.length());
    MR::setMtxTrans(matrix, translation.x, translation.y, translation.z);
    NebulaMtxConcat(matrix, mModelUnbaseMtx.mMtx, matrix);
    pSimple->viewCalcRef(3, pModel);
    mWallShadeOn = 1;
}

void MarioActor::drawShadow() const {
    bool hidden = mHidden || mHiddenByDemo || mModHidden;
    if (hidden) {
        return;
    }

    if (!(mShadowMode & 1)) {
        return;
    }

    if (mPlayerMode == PlayerMode_Teresa) {
        return;
    }

    if (mBeeWallWalk && mMario->mMovementStates.onGround) {
        return;
    }

    if (mMario->isStatusActive(MarioStatus_FpView) && mFpViewHidden) {
        return;
    }

    if (mRushSensor != nullptr && selectNoShadow(mRushSensor)) {
        return;
    }

    if (mMario->mMovementStates.tornado && mMario->mTornadoTimer > 1) {
        return;
    }

    PlayerModel* pSimple = getSimpleModel();
    Pos3f matrix;
    NebulaMtxScale(matrix.toMtxPtr(), mScale.x, mScale.y, mScale.z);
    NebulaMtxConcat(matrix.toMtxPtr(), mModelUnbaseMtx.mMtx, matrix.toMtxPtr());
    NebulaMtxConcat(getModel()->getBaseTRMtx(), matrix.toMtxPtr(), pSimple->getBaseTRMtx());
    mCollisionShadow->calcView(pSimple, 2, getModel());
    if (mMario->mSinkTimer > 180) {
        return;
    }

    if (mMario->mSinkTimer > 128) {
        MR::hideJointAndChildren(pSimple, "Hip");
    }

    if (mMario->mSinkTimer > 150) {
        MR::hideJointAndChildren(pSimple, "ShoulderL");
        MR::hideJointAndChildren(pSimple, "ShoulderR");
    }

    if (mHandsHidden || mMario->mSinkTimer > 120) {
        MR::hideJoint(pSimple, "HandL0");
        MR::hideJoint(pSimple, "HandR0");
    }

    mCollisionShadow->drawAndCaptureTex(pSimple, mPosition);
    if (mBeeWallWalk) {
        mCollisionShadow->mCaptureOn = 0;
    } else {
        mCollisionShadow->mCaptureOn = 1;
    }

    mCollisionShadow->draw();
    MR::showJointAndChildren(pSimple, "JointRoot");
}

void MarioActor::decideShadowMode() {
    if (getMovementStates().shadowFound && !mMario->isNotReflectGlassGround() && !mMario->isStatusActive(MarioStatus_FpView) &&
        (mMario->mPolygonFloorCode == 14 || mMario->mPolygonFloorCode == 33)) {
        mShadowMode = 2;
        f32 distance = mMario->mVerticalSpeed / cNum_decideShadowMode_f1;
        if (distance >= cNum_decideShadowMode_f2) {
            distance = cNum_decideShadowMode_f3;
        }

        u32 maximum = 64;
        distance = 1.0f - distance;
        u8 target = (s32)(static_cast< f32 >(maximum) * distance);
        for (s32 i = 0; i < 4; i++) {
            if (mReflectAlpha < target) {
                mReflectAlpha++;
            } else if (mReflectAlpha > target) {
                mReflectAlpha--;
            }
        }
    } else if (mReflectAlpha) {
        mReflectAlpha--;
        if (mReflectAlpha) {
            mReflectAlpha--;
        }

        if (mReflectAlpha) {
            mReflectAlpha--;
        }

        if (mReflectAlpha) {
            mReflectAlpha--;
        }

        if (!mReflectAlpha) {
            mShadowMode = 1;
        } else {
            mShadowMode = 2;
        }
    } else {
        mShadowMode = 1;
    }

    if (mMario->isSwimming() && MR::isInWater(mCamPos)) {
        if (mWaterReflectAlpha || mCamDirZ.dot(-getGravityVector()) > 0.0f) {
            mShadowMode = 3;
        }

        if (mReflectSimpleAlpha || (mMario->mSwim->getWaterEdgeDist() > 0.0f && mMario->mSwim->getWaterEdgeDist() < cNum_decideShadowMode_f4)) {
            mShadowMode = 7;
        }
    }

    mCollisionShadow->setUpdateFlag();
}

void MarioActor::calcViewSilhouetteModel() {
    PlayerModel* pModel = getModel();
    PlayerModel* pSimple = getSimpleModel();
    Vec3f direction(mPosition - mCamPos);
    MR::normalizeOrZero(&direction);
    NebulaMtxCopy(pModel->getBaseTRMtx(), pSimple->getBaseTRMtx());
    MtxPtr matrix = pSimple->getBaseTRMtx();
    Vec3f x;
    MR::extractMtxXDir(matrix, &x);
    MR::vecKillElement(x, direction, &x);
    Vec3f y;
    MR::extractMtxYDir(matrix, &y);
    MR::vecKillElement(y, direction, &y);
    Vec3f z;
    MR::extractMtxZDir(matrix, &z);
    MR::vecKillElement(z, direction, &z);
    MR::setMtxAxisXYZ(matrix, x, y, z);
    Vec3f root;
    Vec3f translation;
    if (mInDemo) {
        getRealPos("All_Root", &root);
        translation = root - direction * getConst().getTable()->mSilhouetteZoffset;
    } else {
        translation = mPosition;
    }

    MR::setMtxTrans(matrix, translation.x, translation.y, translation.z);
    NebulaMtxConcat(matrix, mModelUnbaseMtx.mMtx, matrix);
    pSimple->viewCalcRef(1, pModel);
}

f32 MarioActor::calcCylinderToCenter(const AreaObj* pAreaObj, Vec3f* pVec) {
    Vec3f upVec;
    MR::calcCylinderUpVec(&upVec, pAreaObj);

    Vec3f centerPos;
    MR::calcCylinderCenterPos(&centerPos, pAreaObj);

    MR::vecKillElement(centerPos - mCenterPos, upVec, pVec);

    if (MR::normalizeOrZero(pVec)) {
        if (mWallShadeDist >= cNum_calcCylinderToCenter_f1) {
            return -1.0f;
        }

        mWallShadeDist += cNum_calcCylinderToCenter_f2;

        if (mWallShadeDist >= cNum_calcCylinderToCenter_f3) {
            mWallShadeDist = cNum_calcCylinderToCenter_f4;
        }

        return -1.0f;
    }

    return MR::getAreaObjArg(pAreaObj, 0);
}

void MarioActor::calcViewBlurModel() {
    PlayerModel* pModel = getModel();
    if (pModel->mBlurDrawMissed) {
        if (mBlurState == 1) {
            mBlurState = 2;
        }

        pModel->mBlurDrawMissed = 0;
    }

    if (MR::isDemoActive()) {
        if (mBlurState) {
            mBlurState = 5;
        }

        return;
    }

    if (mBaseMtxStale) {
        return;
    }

    if (!mBlurState) {
        return;
    }

    if (mBlurState >= 3) {
        mBlurState--;
        return;
    }

    if (mBlurState == 2) {
        for (u32 i = 0; i < cBlurPoseNum; i++) {
            Model::copyPose(&mBlurPoses[0][i], *pModel->getDrawPose());
            Model::copyPose(&mBlurPoses[1][i], *pModel->getDrawPose());
        }

        mBlurPoseIndex = 0;
        mBlurState = 1;
    } else if (mFrameCount % 3 == 0) {
        mBlurPoseIndex = (mBlurPoseIndex + 1) % cBlurPoseNum;
        Model::copyPose(&mBlurPoses[1 - mBlurPoseSet][mBlurPoseIndex], *pModel->getDrawPose());
    }

    mBlurPoseSet = 1 - mBlurPoseSet;
    pModel->mBlurPoseFresh = 1;
}

void MarioActor::calcViewFootPrint() {
    if (!mInRush && mMario->mMovementStates.onGround &&
        (mMario->mTargetWalkSpeedIndex || getMario()->mHitStates.landed || mMario->mWalkSpeed > cNum_calcViewFootPrint_f1 || mMario->isPlayerModeHopper())) {
        if (!mMario->mSinkTimer && !mMario->mDrawStates.baseTransPoint && !mMario->mDrawStates.atEdge && mMario->mHitStates.groundSameSensor && mMario->mHitStates.groundEvenSlope &&
            (mMario->checkCurrentFloorCodeSevere(13) || mMario->checkCurrentFloorCodeSevere(26))) {
            mFootPrint->addPrint(mPosition, mMario->mFrontVec, mMario->mGroundNorm, false);
        }
    }

    if (mFootPrint->isValid(mFrameCount) && MR::isInWater(*mFootPrint->getPrintPos(mFrameCount))) {
        mFootPrint->invalidate(mFrameCount);
    }
}

void MarioActor::drawSilhouette() const {
    bool hidden = isAllHidden();
    if (hidden) {
        return;
    }

    if (mMario->isStatusActive(MarioStatus_Sukekiyo)) {
        return;
    }

    if (mMario->isStatusActive(MarioStatus_Bury)) {
        if (!MR::isExistMapCollision(mCamPos, mPosition - mCamPos)) {
            return;
        }

        if (!MR::isExistMapCollision(mCamPos, mHeadPos - mCamPos)) {
            return;
        }
    }

    if (mMario->mSinkTimer) {
        return;
    }

    if (mPlayerMode == PlayerMode_Teresa) {
        return;
    }

    if (mMario->isStatusActive(MarioStatus_FpView) && mFpViewHidden) {
        return;
    }

    if (mInDemo) {
        return;
    }

    if (mInRush) {
        return;
    }

    PlayerModel* pModel = getSimpleModel();
    pModel->setDrawView(1);
    pModel->mDrawPasses = PlayerModel::Pass_BehindMark;
    pModel->directDraw();
    pModel->mDrawPasses &= ~PlayerModel::Pass_BehindMark;
}
