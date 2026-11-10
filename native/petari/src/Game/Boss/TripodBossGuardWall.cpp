#include "Game/Boss/TripodBossGuardWall.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    struct WallPartPlacementAngle {
        f32 mAngle;
        s32 mStartTiming;
    };

    const WallPartPlacementAngle sWallPartPlacementAngleTable[] = {{0.0f, 4},    {45.0f, 8},   {90.0f, 12},  {135.0f, 16},
                                                                    {180.0f, 20}, {225.0f, 24}, {270.0f, 28}, {315.0f, 32}};
    static const s32 sEventCameraStartTime = NebulaFrameClock::stepsOfMilliseconds(2500);
    static const s32 sEventAnimeStartTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, sStartCameraBlendTime, 20);
    static const s32 sEndCameraBlendTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sRotateSpeed, -0.2f);
    NEBULA_TUNABLE(s32, sMoveSeLength, 58);
};  // namespace

namespace NrvTriPodBossGuardWall {
    NEW_NERVE(TripodBossGuardWallNrvWait, TripodBossGuardWall, Wait);
    NEW_NERVE(TripodBossGuardWallNrvTryDemo, TripodBossGuardWall, TryDemo);
    NEW_NERVE(TripodBossGuardWallNrvDemo, TripodBossGuardWall, Demo);
    NEW_NERVE(TripodBossGuardWallNrvRotate, TripodBossGuardWall, Rotate);
};  // namespace NrvTriPodBossGuardWall

TripodBossGuardWall::TripodBossGuardWall(const char* pName) : LiveActor(pName), mCameraTargetMtx(), mStartTrans(0.0f, 0.0f, 0.0f), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossGuardWall >);
    mBaseMtx.identity();
}

void TripodBossGuardWall::makeActorAppeared() {
    LiveActor::makeActorAppeared();
}

void TripodBossGuardWall::makeActorDead() {
    for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
        mParts[i].makeActorDead();
    }

    LiveActor::makeActorDead();
}

void TripodBossGuardWall::kill() {
    for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
        mParts[i].kill();
    }

    LiveActor::kill();
}

void TripodBossGuardWall::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjDecorationMovement(this);
    mStartTrans = mPosition;
    initModelManagerWithAnm("TripodBossGuardWall", nullptr, false);
    MR::getPlacementArg0NoInit(rIter, &mJointID);
    initSound(4, false);
    initNerve(GET_NERVE(TriPodBossGuardWall, TripodBossGuardWallNrvWait));
    mCameraInfo = MR::createActorCameraInfo(rIter);
    MR::initAnimCamera(this, mCameraInfo, "2ndDemo");
    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnAppear(this, MR::Functor(this, &TripodBossGuardWall::requestStart));
    }

    MR::invalidateClipping(this);
    initParts();
    updateMatrix();
    MR::addTripodBossParts(this);
    makeActorDead();
}

void TripodBossGuardWall::initParts() {
    for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
        mParts[i].setHostMatrix(&mBaseMtx);
        mParts[i].setPlacementAngle(::sWallPartPlacementAngleTable[i].mAngle);
        mParts[i].setStartTiming(::sWallPartPlacementAngleTable[i].mStartTiming);
        mParts[i].initWithoutIter();
    }
}

void TripodBossGuardWall::control() {
    updateMatrix();
}

void TripodBossGuardWall::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}

bool TripodBossGuardWall::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_TRIPODBOSS_STARTED) {
        for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
            mParts[i].makeActorAppeared();
        }

        return true;
    }

    return false;
}

void TripodBossGuardWall::requestStart() {
    setNerve(GET_NERVE(TriPodBossGuardWall, TripodBossGuardWallNrvTryDemo));

    if (MR::tryDamageDemoTripodBoss()) {
        setNerve(GET_NERVE(TriPodBossGuardWall, TripodBossGuardWallNrvDemo));
    }
}

void TripodBossGuardWall::exeWait() {
}

void TripodBossGuardWall::exeTryDemo() {
    if (MR::tryDamageDemoTripodBoss()) {
        setNerve(GET_NERVE(TriPodBossGuardWall, TripodBossGuardWallNrvDemo));
    }
}

void TripodBossGuardWall::exeDemo() {

    if (MR::isStep(this, ::sEventCameraStartTime)) {
        updateCameraTarget();
        CameraTargetArg arg(nullptr, mCameraTargetMtx, nullptr, nullptr);
        MR::startAnimCameraTargetOther(this, mCameraInfo, "2ndDemo", arg, ::sStartCameraBlendTime, 1.0f);
    }

    if (MR::isStep(this, ::sEventAnimeStartTime)) {
        for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
            mParts[i].requestStartDemo();
        }
    }

    if (MR::isGreaterStep(this, ::sEventAnimeStartTime)) {
        bool isAnyPartDemoEnd = true;

        for (s32 i = 0; i < ARRAY_SIZE(mParts); i++) {
            if (!mParts[i].isEndDemo()) {
                isAnyPartDemoEnd = false;
                break;
            }
        }

        if (MR::isLessStep(this, ::sMoveSeLength + ::sEventAnimeStartTime)) {
            MR::startLevelSound(this, "SE_BM_LV_TRIPOD_WALL_UP");
        }

        if (isAnyPartDemoEnd) {
            MR::endAnimCamera(this, mCameraInfo, "2ndDemo", ::sEndCameraBlendTime, true);
            MR::requestEndDamageDemoTripodBoss();
            setNerve(GET_NERVE(TriPodBossGuardWall, TripodBossGuardWallNrvRotate));
        }
    }
}

void TripodBossGuardWall::exeRotate() {
    mRotation.y += ::sRotateSpeed;

    MR::repeatDegree(&mRotation.y);
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_C_WALL_MOVE");

    if (MR::isEndBreakDownDemoTripodBoss()) {
        kill();
    }
}

void TripodBossGuardWall::updateMatrix() {
    MR::makeMtxTR(mBaseMtx, mStartTrans, mRotation);
    MR::concatTripodBossAttachJointMatrix(&mBaseMtx, mJointID);
    mBaseMtx.getTrans(mPosition);
}

void TripodBossGuardWall::updateCameraTarget() {
    Vec3f front;
    Vec3f up;
    Vec3f fromPlayer = mPosition - *MR::getPlayerPos();
    front.x = fromPlayer.x;
    front.y = fromPlayer.y;
    front.z = fromPlayer.z;

    mBaseMtx.getYDir(up);
    front.orthogonalize(up);

    if (MR::isNearZero(front)) {
        mBaseMtx.getZDir(front);
    } else {
        MR::normalize(&front);
    }

    Pos3f mtx;
    mtx.identity();
    MR::makeMtxUpFrontPos(&mtx, up, front, mPosition);
    mCameraTargetMtx->setMtx(mtx);
}
