#include "Game/Enemy/EyeBeamer.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>

namespace NrvEyeBeamer {
    NEW_NERVE(EyeBeamerNrvDemoStartWait, EyeBeamer, DemoStartWait);
    NEW_NERVE(EyeBeamerNrvDemoWait, EyeBeamer, DemoWait);
    NEW_NERVE(EyeBeamerNrvDemoTurn, EyeBeamer, DemoTurn);
    NEW_NERVE(EyeBeamerNrvDemoGotoPatrol, EyeBeamer, DemoGotoPatrol);
    NEW_NERVE(EyeBeamerNrvWait, EyeBeamer, Wait);
    NEW_NERVE(EyeBeamerNrvTurn, EyeBeamer, Turn);
    NEW_NERVE(EyeBeamerNrvGotoPatrol, EyeBeamer, GotoPatrol);
    NEW_NERVE(EyeBeamerNrvPatrol, EyeBeamer, Patrol);
};  // namespace NrvEyeBeamer

EyeBeamer::EyeBeamer(const char* pName)
    : LiveActor(pName), mRailMover(nullptr), mBeamMdl(nullptr), mBeamBloom(nullptr), mBeamVolumeDrawer(nullptr), mInitQuat(0, 0, 0, 1), mPoseQuat(0, 0, 0, 1),
      mBasePos(0, 0, 0), mGotoStartPos(0, 0, 0), mRailStartPos(0, 0, 0), mClipCenter(0, 0, 0), mBeamLength(2000.0f), mWaterSurfaceHeight(-1.0f), mIsInMercatorCube(false) {
    mWaterSurfaceMtx.identity();
    mBeamMtx.identity();
}

void EyeBeamer::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mIsInMercatorCube = MR::isInAreaObj("MercatorCube", mPosition);
    initModel();
    MR::makeQuatFromRotate(&mPoseQuat, this);
    mInitQuat.x = mPoseQuat.x;
    mInitQuat.y = mPoseQuat.y;
    mInitQuat.z = mPoseQuat.z;
    mInitQuat.w = mPoseQuat.w;
    mBasePos.set(mPosition);
    MR::getPlacementArg0NoInit(rIter, &mBeamLength);
    MR::getPlacementArg1NoInit(rIter, &mWaterSurfaceHeight);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "beam", 8, 140.0f + (0.5f * mBeamLength), Vec3f(0.0f, 0.5f * (-mBeamLength), 0.0f));
    initSound(4, 0);
    MR::initCollisionParts(this, "EyeBeamer", getSensor("body"), 0);
    initRailMoveFunction(rIter);
    mGotoStartPos.set(mBasePos);
    MR::calcRailStartPos(&mRailStartPos, this);
    initEffectKeeper(0, 0);
    MR::setEffectHostMtx(this, "BeamSurface", mWaterSurfaceMtx);
    initStartNerve(rIter);

    if (MR::useStageSwitchReadA(this, rIter))
        MR::listenStageSwitchOnA(this, MR::Functor(this, &EyeBeamer::requestStartPatrol));

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(EyeBeamer, EyeBeamerNrvDemoTurn), "アイビーマー回転");
        MR::registerDemoActionNerve(this, GET_NERVE(EyeBeamer, EyeBeamerNrvDemoGotoPatrol), "アイビーマー降下");
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoStartWait));
    }
    MR::setGroupClipping(this, rIter, 0x20);
    MR::startBck(this, "EyeBeamer");
    makeActorAppeared();
}

void EyeBeamer::initStartNerve(const TableIter& rIter) {
    s32 arg = -1;
    MR::getPlacementArg2WithInit(rIter, &arg);

    switch (arg) {
    case -1:
        initNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait));
        break;
    case 0:
        initNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
        break;
    default:
        initNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait));
        break;
    }
}

void EyeBeamer::initModel() {
    initModelManagerWithAnm("EyeBeamer", 0, false);
    mBeamVolumeDrawer = new VolumeModelDrawer("ビームボリューム", "EyeBeamerBeamVolume", mBeamMtx);
    mBeamBloom = new ModelObj("ビームブルーム", "EyeBeamerBeamBloom", mBeamMtx, MR::DrawBufferType_BloomModel, -2, -2, false);
    mBeamBloom->initWithoutIter();
    MR::invalidateClipping(mBeamBloom);
    mBeamMdl = new ModelObj("ビーム", "EyeBeamerBeam", mBeamMtx, MR::DrawBufferType_IndirectMapObj, -2, -2, false);
    mBeamMdl->initWithoutIter();
    MR::invalidateClipping(mBeamMdl);
    MR::startBtk(mBeamMdl, "EyeBeamerBeam");
    MR::registerDemoSimpleCastAll(mBeamBloom);
    MR::registerDemoSimpleCastAll(mBeamMdl);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_MapObjStrongLight, MR::DrawType_VolumeModel);
}

void EyeBeamer::initAfterPlacement() {
    updatePoseAndTrans();
    Vec3f stack_80;
    MR::calcUpVec(&stack_80, this);
    stack_80.negate();
    Vec3f stack_74;
    stack_74.set(mPosition - (stack_80 * 300.0f));
    Vec3f stack_68;
    f32 dist;
    if (MR::getFirstPolyOnLineToMapExceptActor(&stack_68, 0, stack_74, stack_80 * (300.0f + mBeamLength), this)) {
        dist = stack_68.distance(mPosition);
        mClipCenter.set((stack_68 + mPosition) * 0.5);
        dist = 300.0f + 0.5f * dist;
    } else {
        mClipCenter.set(mPosition + ((stack_80 * mBeamLength) * 0.5f));
        dist = 300.0f + (0.5f * mBeamLength);
    }

    MR::setClippingTypeSphere(this, dist, &mClipCenter);
}

void EyeBeamer::initRailMoveFunction(const TableIter& rIter) {
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
        mRailMover->start();
        MR::moveCoordToStartPos(this);
    }
}

void EyeBeamer::updatePoseAndTrans() {
    Pos3f stack_38;
    stack_38.identity();
    stack_38.setQuat(mPoseQuat);
    if (mIsInMercatorCube) {
        Pos3f stack_8;
        stack_8.identity();
        MR::convertMercatorPlaneToSphereTransAndRotate(&mPosition, &stack_8, mBasePos, true);
        stack_38.concat(stack_8, stack_38);
    } else {
        mPosition.set(mBasePos);
    }
    stack_38.setTrans(mPosition);
    MR::setBaseTRMtx(this, stack_38);
    MR::setBaseScale(this, mScale);
}

void EyeBeamer::updateWaterSurfaceMtx() {
    if (mWaterSurfaceHeight < 0.0f)
        return;

    mWaterSurfaceMtx.setQuat(mPoseQuat);
    Vec3f stack_38;
    mWaterSurfaceMtx.getYDir(stack_38);
    Vec3f stack_2C;
    Vec3f stack_20(mBasePos - (stack_38 * (mWaterSurfaceHeight + (stack_38).dot(mBasePos - mRailStartPos))));
    stack_2C.set(stack_20);

    if (mIsInMercatorCube) {
        Pos3f stack_44;
        stack_44.identity();
        MR::convertMercatorPlaneToSphereTransAndRotate(&stack_2C, &stack_44, stack_2C, true);
        mWaterSurfaceMtx.concat(stack_44, mWaterSurfaceMtx);
    }
    mWaterSurfaceMtx.setTrans(stack_2C);
}

void EyeBeamer::control() {
    updatePoseAndTrans();
    updateWaterSurfaceMtx();
    MR::startLevelSound(this, "SE_OJ_LV_EYE_BEAMER_MOVE");
}

void EyeBeamer::startClipped() {
    LiveActor::startClipped();
    mBeamBloom->startClipped();
    mBeamMdl->startClipped();
}

void EyeBeamer::endClipped() {
    LiveActor::endClipped();
    mBeamBloom->endClipped();
    mBeamMdl->endClipped();
}

void EyeBeamer::draw() const {
    mBeamVolumeDrawer->draw();
}

void EyeBeamer::calcAnim() {
    LiveActor::calcAnim();
    MtxPtr mtx = MR::getJointMtx(this, "Beam");
    mBeamMtx.setInline(mtx);
    f32 one = 1.0f;
    f32 temp = mBeamLength / 2000.0f;
    MR::preScaleMtx(mBeamMtx, Vec3f(one, temp, one));
}

void EyeBeamer::calcAndSetBaseMtx() {
}

void EyeBeamer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isOnBeam() && MR::isSensorPlayer(pReceiver) && isInBeamRange(*MR::getPlayerPos())) {
        MR::sendMsgEnemyAttackHeatBeam(pReceiver, pSender);
    }
}

void EyeBeamer::requestStartPatrol() {
    if (isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait))) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvTurn));
        MR::invalidateClipping(this);
    }
}

bool EyeBeamer::tryGotoPatrol() {
    if (MR::isGreaterStep(this, 300)) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvGotoPatrol));
        return true;
    }
    return false;
}

bool EyeBeamer::tryPatrol() {
    if (MR::isGreaterStep(this, 500)) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
        return true;
    }
    return false;
}

void EyeBeamer::exeDemoStartWait() {
}

void EyeBeamer::exeDemoWait() {
    if (MR::isDemoLastStep()) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
    }
}

void EyeBeamer::exeDemoTurn() {
    if (MR::isFirstStep(this))
        MR::startSound(this, "SE_OJ_EYE_BEAMER_TURN");

    f32 rate = MR::calcDemoPartStepRate("アイビーマー回転");
    f32 easeIn = MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f);
    Quat4f stack_30;
    Vec3f stack_24;
    mInitQuat.getXDir(stack_24);
    stack_30.setRotate(stack_24, PI * easeIn);
    NebulaQuatMultiply((Quaternion*)&stack_30, (Quaternion*)&mInitQuat, (Quaternion*)&mPoseQuat);

    if (MR::isDemoPartLastStep("アイビーマー回転")) {
        Quat4f stack_14;
        Vec3f stack_8;
        mInitQuat.getXDir(stack_8);
        stack_14.setRotate(stack_8, PI);
        NebulaQuatMultiply((Quaternion*)&stack_14, (Quaternion*)&mInitQuat, (Quaternion*)&mPoseQuat);
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoWait));
    }
}

void EyeBeamer::exeDemoGotoPatrol() {
    f32 rate = MR::calcDemoPartStepRate("アイビーマー降下");
    f32 easeInOut = MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f);
    MR::vecBlend(mGotoStartPos, mRailStartPos, &mBasePos, easeInOut);
    if (MR::isDemoPartLastStep("アイビーマー降下"))
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoWait));
}

void EyeBeamer::exeWait() {
}

void EyeBeamer::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_EYE_BEAMER_TURN");
    }
    f32 easeInOut = MR::calcNerveEaseInOutRate(this, 300);
    Quat4f stack_14;
    Vec3f stack_8;
    mInitQuat.getXDir(stack_8);
    stack_14.setRotate(stack_8, PI * easeInOut);
    NebulaQuatMultiply((Quaternion*)&stack_14, (Quaternion*)&mInitQuat, (Quaternion*)&mPoseQuat);

    if (tryGotoPatrol())
        return;
}

void EyeBeamer::exeGotoPatrol() {
    f32 easeInOut = MR::calcNerveEaseInOutRate(this, 500);
    MR::vecBlend(mGotoStartPos, mRailStartPos, &mBasePos, easeInOut);

    if (tryPatrol())
        return;
}

void EyeBeamer::exePatrol() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        Vec3f stack_48;
        Quat4f stack_38;
        mInitQuat.getXDir(stack_48);
        stack_38.setRotate(stack_48, PI);
        NebulaQuatMultiply((Quaternion*)&stack_38, (Quaternion*)&mInitQuat, (Quaternion*)&mPoseQuat);
        if (mWaterSurfaceHeight > 0)
            MR::emitEffect(this, "BeamSurface");
    }
    Vec3f stack_2C;
    MR::calcUpVec(&stack_2C, this);
    stack_2C.negate();
    mClipCenter.set(mPosition + ((stack_2C * mBeamLength) * 0.5));
    MR::setClippingTypeSphere(this, 300.0f + (0.5f * mBeamLength), &mClipCenter);
    mRailMover->movement();
    mBasePos.set(mRailMover->mStartPos);
}

bool EyeBeamer::isInBeamRange(const Vec3f& rVec) const {
    Vec3f stack_44;
    MR::calcUpVec(&stack_44, this);
    stack_44.negate();
    Vec3f stack_38;
    mBeamMtx.getTrans(stack_38);
    f32 dot = stack_44.dot(rVec - stack_38);

    if (dot < 0.0f || mBeamLength < dot)
        return false;
    else {
        Vec3f stack_2C;
        stack_2C.set(stack_38 + (stack_44 * dot));
        f32 dist = stack_2C.distance(rVec);

        if (dist < 140.0f)
            return true;
    }

    return false;
}

bool EyeBeamer::isOnBeam() const {
    if (isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoStartWait)) || isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait)) ||
        isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol)))
        return true;

    return false;
}
