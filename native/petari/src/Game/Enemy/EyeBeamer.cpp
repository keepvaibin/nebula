#include "Game/Enemy/EyeBeamer.hpp"
#include "Game/System/NebulaFrameClock.hpp"
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
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f3, "initAfterPlacement.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f4, "initAfterPlacement.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePatrol_f1, "exePatrol.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePatrol_f2, "exePatrol.f2", 0.5f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_tryPatrol_1, "tryPatrol.1", 500);

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

namespace {
    // The beam model is this long at scale 1.
    NEBULA_TUNABLE(f32, cBeamModelLength, 2000.0f);
    NEBULA_TUNABLE(f32, cBeamRadius, 140.0f);
    NEBULA_TUNABLE(f32, cClipMargin, 300.0f);
};  // namespace

EyeBeamer::EyeBeamer(const char* pName)
    : LiveActor(pName), mRailMover(nullptr), mBeamBloom(nullptr), mBeamMdl(nullptr), mBeamVolumeDrawer(nullptr), mInitQuat(0, 0, 0, 1),
      mPoseQuat(0, 0, 0, 1), mBasePos(0, 0, 0), mGotoStartPos(0, 0, 0), mRailStartPos(0, 0, 0), mClipCenter(0, 0, 0),
      mBeamLength(cBeamModelLength), mWaterSurfaceHeight(-1.0f), mIsInMercatorCube(false) {
    mWaterSurfaceMtx.identity();
    mBeamMtx.identity();
}

void EyeBeamer::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mIsInMercatorCube = MR::isInAreaObj("MercatorCube", mPosition);
    initModel();
    MR::makeQuatFromRotate(&mPoseQuat, this);
    mInitQuat = mPoseQuat;
    mBasePos.set(mPosition);
    MR::getPlacementArg0NoInit(rIter, &mBeamLength);
    MR::getPlacementArg1NoInit(rIter, &mWaterSurfaceHeight);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "beam", 8, cBeamRadius + (cNum_init_f1 * mBeamLength), Vec3f(0.0f, cNum_init_f2 * (-mBeamLength), 0.0f));
    initSound(4, 0);
    MR::initCollisionParts(this, "EyeBeamer", getSensor("body"), 0);
    initRailMoveFunction(rIter);
    mGotoStartPos.set(mBasePos);
    MR::calcRailStartPos(&mRailStartPos, this);
    initEffectKeeper(0, 0);
    MR::setEffectHostMtx(this, "BeamSurface", mWaterSurfaceMtx);
    initStartNerve(rIter);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &EyeBeamer::requestStartPatrol));
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(EyeBeamer, EyeBeamerNrvDemoTurn), "アイビーマー回転");
        MR::registerDemoActionNerve(this, GET_NERVE(EyeBeamer, EyeBeamerNrvDemoGotoPatrol), "アイビーマー降下");
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoStartWait));
    }
    MR::setGroupClipping(this, rIter, 32);
    MR::startBck(this, "EyeBeamer");
    makeActorAppeared();
}

// Obj_arg2: 0 patrols from the start; anything else waits for switch A or the demo.
void EyeBeamer::initStartNerve(const TableIter& rIter) {
    s32 startMode = -1;
    MR::getPlacementArg2WithInit(rIter, &startMode);

    if (startMode == 0) {
        initNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
    } else {
        initNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait));
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

// The clipping sphere covers the beam down to the first map polygon it meets.
void EyeBeamer::initAfterPlacement() {
    updatePoseAndTrans();
    Vec3f down;
    MR::calcUpVec(&down, this);
    down.negate();
    Vec3f rayStart;
    rayStart.set(mPosition - (down * cClipMargin));
    Vec3f hitPos;
    f32 clipRadius;
    if (MR::getFirstPolyOnLineToMapExceptActor(&hitPos, 0, rayStart, down * (cClipMargin + mBeamLength), this)) {
        f32 distance = hitPos.distance(mPosition);
        mClipCenter.set((hitPos + mPosition) * cNum_initAfterPlacement_f1);
        clipRadius = cClipMargin + cNum_initAfterPlacement_f2 * distance;
    } else {
        mClipCenter.set(mPosition + ((down * mBeamLength) * cNum_initAfterPlacement_f3));
        clipRadius = cClipMargin + (cNum_initAfterPlacement_f4 * mBeamLength);
    }

    MR::setClippingTypeSphere(this, clipRadius, &mClipCenter);
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

// Pose from mPoseQuat at mBasePos; inside a mercator cube the plane position wraps onto the sphere.
void EyeBeamer::updatePoseAndTrans() {
    Pos3f baseMtx;
    baseMtx.identity();
    baseMtx.setQuat(mPoseQuat);
    if (mIsInMercatorCube) {
        Pos3f sphereRotate;
        sphereRotate.identity();
        MR::convertMercatorPlaneToSphereTransAndRotate(&mPosition, &sphereRotate, mBasePos, true);
        baseMtx.concat(sphereRotate, baseMtx);
    } else {
        mPosition.set(mBasePos);
    }
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    MR::setBaseScale(this, mScale);
}

// Obj_arg1: the water surface's height above the rail start; it holds still while the beamer moves along its up axis.
void EyeBeamer::updateWaterSurfaceMtx() {
    if (mWaterSurfaceHeight < 0.0f) {
        return;
    }

    mWaterSurfaceMtx.setQuat(mPoseQuat);
    Vec3f up;
    mWaterSurfaceMtx.getYDir(up);
    Vec3f surfacePos(mBasePos - (up * (mWaterSurfaceHeight + up.dot(mBasePos - mRailStartPos))));

    if (mIsInMercatorCube) {
        Pos3f sphereRotate;
        sphereRotate.identity();
        MR::convertMercatorPlaneToSphereTransAndRotate(&surfacePos, &sphereRotate, surfacePos, true);
        mWaterSurfaceMtx.concat(sphereRotate, mWaterSurfaceMtx);
    }
    mWaterSurfaceMtx.setTrans(surfacePos);
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
    mBeamMtx.setInline(MR::getJointMtx(this, "Beam"));
    MR::preScaleMtx(mBeamMtx, Vec3f(1.0f, mBeamLength / cBeamModelLength, 1.0f));
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
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvGotoPatrol));
        return true;
    }
    return false;
}

bool EyeBeamer::tryPatrol() {
    if (MR::isGreaterStep(this, cStep_tryPatrol_1)) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
        return true;
    }
    return false;
}

// The pose turned by angle about the initial X axis; PI is upside down, beam pointing the other way.
void EyeBeamer::setPoseTurned(f32 angle) {
    Vec3f axis;
    mInitQuat.getXDir(axis);
    Quat4f turn;
    turn.setRotate(axis, angle);
    NebulaQuatMultiply(&turn, &mInitQuat, &mPoseQuat);
}

void EyeBeamer::exeDemoStartWait() {
}

void EyeBeamer::exeDemoWait() {
    if (MR::isDemoLastStep()) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
    }
}

void EyeBeamer::exeDemoTurn() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_EYE_BEAMER_TURN");
    }

    f32 rate = MR::calcDemoPartStepRate("アイビーマー回転");
    setPoseTurned(PI * MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f));

    if (MR::isDemoPartLastStep("アイビーマー回転")) {
        setPoseTurned(PI);
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoWait));
    }
}

void EyeBeamer::exeDemoGotoPatrol() {
    f32 rate = MR::calcDemoPartStepRate("アイビーマー降下");
    MR::vecBlend(mGotoStartPos, mRailStartPos, &mBasePos, MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f));
    if (MR::isDemoPartLastStep("アイビーマー降下")) {
        setNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoWait));
    }
}

void EyeBeamer::exeWait() {
}

void EyeBeamer::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_EYE_BEAMER_TURN");
    }

    setPoseTurned(PI * MR::calcNerveEaseInOutRate(this, 300));
    tryGotoPatrol();
}

void EyeBeamer::exeGotoPatrol() {
    MR::vecBlend(mGotoStartPos, mRailStartPos, &mBasePos, MR::calcNerveEaseInOutRate(this, 500));
    tryPatrol();
}

void EyeBeamer::exePatrol() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        setPoseTurned(PI);
        if (mWaterSurfaceHeight > 0.0f) {
            MR::emitEffect(this, "BeamSurface");
        }
    }
    Vec3f down;
    MR::calcUpVec(&down, this);
    down.negate();
    mClipCenter.set(mPosition + ((down * mBeamLength) * cNum_exePatrol_f1));
    MR::setClippingTypeSphere(this, cClipMargin + (cNum_exePatrol_f2 * mBeamLength), &mClipCenter);
    mRailMover->movement();
    mBasePos.set(mRailMover->mStartPos);
}

// Within cBeamRadius of the beam's axis, between its root and its length.
bool EyeBeamer::isInBeamRange(const Vec3f& rPos) const {
    Vec3f down;
    MR::calcUpVec(&down, this);
    down.negate();
    Vec3f root;
    mBeamMtx.getTrans(root);
    f32 along = down.dot(rPos - root);

    if (along < 0.0f || mBeamLength < along) {
        return false;
    }

    Vec3f axisPoint;
    axisPoint.set(root + (down * along));
    return axisPoint.distance(rPos) < cBeamRadius;
}

bool EyeBeamer::isOnBeam() const {
    return isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvDemoStartWait)) || isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvWait)) ||
           isNerve(GET_NERVE(EyeBeamer, EyeBeamerNrvPatrol));
}
