#include "Game/Enemy/StringSpider.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/AnimStampController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactPointer_f1, "reactPointer.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactPointer_f2, "reactPointer.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactStringForce_f1, "reactStringForce.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactStringForce_f2, "reactStringForce.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactStringForce_f3, "reactStringForce.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPosture_f1, "calcPosture.f1", 0.03f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDownStart_f1, "exeDownStart.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDownEnd_f1, "exeDownEnd.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDown_f1, "exeDown.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDown_f2, "exeDown.f2", 230.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeUp_f1, "exeUp.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeUp_f2, "exeUp.f2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f1, "exeSearch.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f2, "exeSearch.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f3, "exeSearch.f3", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f4, "exeSearch.f4", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f5, "exeSearch.f5", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f6, "exeSearch.f6", 1600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f1, "exeAttack.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f2, "exeAttack.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f3, "exeAttack.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHit_f1, "exeHit.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f1, "exeSpinned.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f2, "exeSpinned.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f3, "exeSpinned.f3", 24.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f4, "exeSpinned.f4", 51.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f5, "exeSpinned.f5", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinned_f6, "exeSpinned.f6", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrampleFall_f1, "exeTrampleFall.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f1, "exePointerGripped.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f2, "exePointerGripped.f2", 0.125f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f3, "exePointerGripped.f3", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f4, "exePointerGripped.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f5, "exePointerGripped.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointerGripped_f6, "exePointerGripped.f6", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_extendString_f1, "extendString.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f2, "attackSensor.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f3, "attackSensor.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f4, "attackSensor.f4", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f5, "attackSensor.f5", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f6, "attackSensor.f6", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f2, "receiveMsgPlayerAttack.f2", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startSwingLevelSound_f1, "startSwingLevelSound.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startSwingLevelSound_f2, "startSwingLevelSound.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 0.063f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_1, "control.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_2, "control.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_3, "control.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_applyVelocityDampAndGravity_1, "control.applyVelocityDampAndGravity.1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_applyVelocityDampAndGravity_2, "control.applyVelocityDampAndGravity.2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_applyVelocityDampAndGravity_3, "control.applyVelocityDampAndGravity.3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_applyVelocityDampAndGravity_4, "control.applyVelocityDampAndGravity.4", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_applyVelocityDampAndGravity_5, "control.applyVelocityDampAndGravity.5", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrampleFall_setBinderRadius_1, "exeTrampleFall.setBinderRadius.1", 10.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeTrampleFall_1, "exeTrampleFall.1", 5);
NEBULA_TUNABLE_NAMED(s32, cStep_exeWallHitDeath_1, "exeWallHitDeath.1", 35);

namespace {
    // Swing sounds: volume 0 to 100 over these speeds.
    NEBULA_TUNABLE(f32, cSwing1MinSpeed, 2.0f);
    NEBULA_TUNABLE(f32, cSwing1MaxSpeed, 12.0f);
    NEBULA_TUNABLE(f32, cSwing2MinSpeed, 5.0f);
    NEBULA_TUNABLE(f32, cSwing2MaxSpeed, 25.0f);
    // Collision sound: volume 0 to cCollisionMaxVolume over speed 0 to cCollisionFullSpeed.
    NEBULA_TUNABLE(f32, cCollisionFullSpeed, 15.0f);
    NEBULA_TUNABLE(f32, cCollisionMaxVolume, 100.0f);
    // Spin sound: volume cSpinMinVolume..100 as the spin slows, sub level cSpinMinSubLevel..24.
    NEBULA_TUNABLE(f32, cSpinMinVolume, 40.0f);
    NEBULA_TUNABLE(f32, cSpinMinSubLevel, 5.0f);
    // A spin or star piece knocks the spider 30 away along the floor; the side and up parts are zero.
    NEBULA_TUNABLE(f32, cSpinHitSpeed, 30.0f);
    NEBULA_TUNABLE(f32, cSpinHitSideSpeed, -0.0f);
    NEBULA_TUNABLE(f32, cSpinHitUpSpeed, 0.0f);
    // The string never gets shorter than this share of its placed length (Obj_arg0).
    NEBULA_TUNABLE(f32, cMinStringRate, 0.333f);
    const s32 cSpinnedSteps = NebulaFrameClock::stepsOfSeconds(8);
    NEBULA_TUNABLE(f32, cNoticeDistance, 1400.0f);
};  // namespace

namespace NrvStringSpider {
    NEW_NERVE_ONEND(HostTypeNrvWait, StringSpider, Wait, Wait);
    NEW_NERVE(HostTypeNrvDownStart, StringSpider, DownStart);
    NEW_NERVE(HostTypeNrvDownEnd, StringSpider, DownEnd);
    NEW_NERVE(HostTypeNrvDown, StringSpider, Down);
    NEW_NERVE(HostTypeNrvUp, StringSpider, Up);
    NEW_NERVE(HostTypeNrvSearch, StringSpider, Search);
    NEW_NERVE(HostTypeNrvAttack, StringSpider, Attack);
    NEW_NERVE(HostTypeNrvHit, StringSpider, Hit);
    NEW_NERVE(HostTypeNrvSpinned, StringSpider, Spinned);
    NEW_NERVE(HostTypeNrvTrampleFall, StringSpider, TrampleFall);
    NEW_NERVE(HostTypeNrvWallHitDeath, StringSpider, WallHitDeath);
    NEW_NERVE(HostTypeNrvTrampleDeath, StringSpider, WallHitDeath);
    NEW_NERVE(HostTypeNrvBlow, StringSpider, Blow);
    NEW_NERVE_ONEND(HostTypeNrvPointerGripped, StringSpider, PointerGripped, PointerGripped);
};  // namespace NrvStringSpider

StringSpider::StringSpider(const char* pName)
    : LiveActor(pName), mSpinHitController(nullptr), mScaleController(nullptr), mStampController(nullptr), mString(nullptr), mFixedPos(nullptr),
      mPoseQuat(0, 0, 0, 1), mStringLengthMax(300.0f), mStringLength(300.0f), mIsPointerPulled(false), mPointerStep(0), mSwingFront(nullptr), mSwingSide(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< StringSpider >);
}

// A spider on a fixed swing line (Obj_arg1 == 2) attacks sooner.
s32 StringSpider::getSearchToAttackTime() {
    if (mSwingFront == nullptr) {
        return 300;
    }

    return 60;
}

void StringSpider::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);

        s32 stringLength = -1;
        MR::getPlacementArg0NoInit(rIter, &stringLength);
        if (stringLength != -1) {
            f32 length = stringLength;
            mStringLength = length * cMinStringRate;
            mStringLengthMax = length;
        }

        s32 swingMode = -1;
        MR::getPlacementArg1NoInit(rIter, &swingMode);
        if (swingMode == 2) {
            mSwingFront = new Vec3f();
            mSwingSide = new Vec3f();
        }

        if (MR::useStageSwitchWriteDead(this, rIter)) {
            MR::offSwitchDead(this);
        }
        MR::useStageSwitchReadA(this, rIter);
    }

    initModelManagerWithAnm("StringSpider", nullptr, false);
    MR::makeQuatFromRotate(&mPoseQuat, this);

    if (mSwingFront != nullptr) {
        mPoseQuat.getZDir(*mSwingFront);
        mPoseQuat.getXDir(*mSwingSide);
    }

    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initBinder(cNum_init_f1 * mScale.y, 0.0f, 0);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, mScale.y * cNum_init_f2, Vec3f(0.0f, -cNum_init_f3, cNum_init_f4));
    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(StringSpider, HostTypeNrvWait));
    MR::initStarPointerTarget(this, cNum_init_f5, Vec3f(0, 0, 0));
    MR::declareStarPiece(this, 6);
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    mSpinHitController = new SpinHitController(this, 3, false);
    mScaleController = new AnimScaleController(nullptr);
    mStampController = new AnimStampController();
    MR::onCalcGravity(this);

    // Obj_arg2: a cylinder shadow dropped this far; otherwise a sphere shadow.
    s32 shadowLength = -1;
    MR::getPlacementArg2NoInit(rIter, &shadowLength);
    if (shadowLength > 0) {
        MR::initShadowVolumeCylinder(this, mScale.y * cNum_init_f6);
        MR::setShadowDropLength(this, nullptr, shadowLength);
    } else {
        MR::initShadowVolumeSphere(this, mScale.y * cNum_init_f7);
    }

    mFixedPos = new FixedPosition(static_cast< MtxPtr >(nullptr),Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    MR::setGroupClipping(this, rIter, 16);
    mString = new StringSpiderString("ストリングスパイダーの糸");
    MR::resetPosition(mString, mPosition);
    mString->makeActorAppeared();
    makeActorAppeared();
    MR::emitEffect(this, "EyeLight");
}

// The string hangs from the map polygon within 100 above; it follows that polygon if it moves.
void StringSpider::initAfterPlacement() {
    Triangle triangle = Triangle();
    Vec3f rootPos, upVec, localRootPos;
    MR::calcUpVec(&upVec, this);
    upVec.setLength(cNum_initAfterPlacement_f1);

    if (MR::getFirstPolyOnLineToMap(&rootPos, &triangle, mPosition - upVec * cNum_initAfterPlacement_f2, upVec)) {
        triangle.getBaseInvMtx()->mult(rootPos, localRootPos);
        MR::resetPosition(mString, rootPos);
        mFixedPos->setBaseMtx(triangle.getBaseMtx()->mMtx);
        mFixedPos->setLocalTrans(localRootPos);
    } else {
        mFixedPos = nullptr;
    }
}

void StringSpider::kill() {
    LiveActor::kill();
    mString->kill();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void StringSpider::startClipped() {
    LiveActor::startClipped();
    mString->startClipped();
    MR::deleteEffect(this, "EyeLight");
}

void StringSpider::endClipped() {
    LiveActor::endClipped();
    mString->endClipped();
    MR::emitEffect(this, "EyeLight");
}

// Pushes the swing while hanging straight down: an idle swing to the side (keeping the swing's direction), or the
// attack swing forward (back for the 30-step wind-up).
void StringSpider::doSwingAction(f32 accel, bool isSideSwing) {
    Vec3f toRoot(mString->mPosition);
    Vec3f swingDir, pushDir;
    toRoot.sub(mPosition);
    MR::normalizeOrZero(&toRoot);
    if (!MR::isOppositeDirection(mGravity, toRoot, 1.0f)) {
        return;
    }

    if (isSideSwing) {
        if (mSwingFront != nullptr) {
            return;
        }
        mPoseQuat.getXDir(swingDir);
    } else {
        if (mSwingFront == nullptr) {
            mPoseQuat.getZDir(swingDir);
        } else {
            swingDir.set(*mSwingFront);
        }

        if (MR::isFirstStep(this)) {
            MR::startBck(this, "PreAttack");
        }

        if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
            MR::startBck(this, "Attack");
        }

        if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
            swingDir = -swingDir;
        }
    }

    MR::vecKillElement(mVelocity, toRoot, &pushDir);
    if (!isSideSwing || MR::isNearZero(pushDir)) {
        pushDir.set(swingDir);
    } else {
        swingDir.scale(swingDir.dot(pushDir));
        MR::normalizeOrZero(&swingDir);
        pushDir.set(swingDir);
    }

    mVelocity += pushDir * accel;
}

void StringSpider::reactPointer() {
    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvWallHitDeath)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath))) {
        mIsPointerPulled = false;
        mPointerStep = 0;
        return;
    }

    if (mPointerStep > 0) {
        mPointerStep--;
    }

    // A 2P pointer flick faster than 20 across the spider knocks it along the flick, every 30 steps at most.
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        if (!mIsPointerPulled && mPointerStep == 0) {
            Vec2f screenVel = *MR::getStarPointerScreenVelocity(1);
            if (screenVel.length() < cNum_reactPointer_f1) {
                return;
            }

            Vec3f flick;
            MR::getStarPointerWorldVelocityDirection(&flick, 1);
            flick.setLength(cNum_reactPointer_f2);
            mVelocity.add(flick);
            mIsPointerPulled = true;
            Vec3f hitPos(mPosition);
            flick.setLength(getSensor("body")->mRadius);
            hitPos.add(flick);
            MR::emitEffectHit(this, hitPos, nullptr);
            mPointerStep = NebulaFrameClock::stepsOfMilliseconds(500);
            MR::start2PAttackAssistSound();
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned));
        }
        mIsPointerPulled = true;
    } else {
        mIsPointerPulled = false;
    }
}

// Past the string's length: bounces off it and is pulled back by a tenth of the excess; 1000 past, the string snaps.
void StringSpider::reactStringForce() {
    f32 distance;
    Vec3f toRoot;
    if (isOverStringLength(&toRoot, &distance)) {
        if ((cNum_reactStringForce_f1 + mStringLength) < distance) {
            kill();
        } else {
            MR::normalizeOrZero(&toRoot);
            if (!isNerve(GET_NERVE(StringSpider, HostTypeNrvPointerGripped))) {
                MR::calcReflectionVector(&mVelocity, toRoot, cNum_reactStringForce_f2, 1.0f);
            }

            f32 excess = distance - mStringLength;
            mVelocity += toRoot * excess * cNum_reactStringForce_f3;
        }
    }
}

bool StringSpider::isOverStringLength(Vec3f* pToRoot, f32* pDistance) {
    Vec3f toRoot = calcToRootVector();
    f32 distance = toRoot.length();
    if (pToRoot != nullptr) {
        pToRoot->set(toRoot);
    }

    if (pDistance != nullptr) {
        *pDistance = distance;
    }

    return mStringLength < distance;
}

void StringSpider::control() {
    if (mFixedPos != nullptr) {
        mFixedPos->calc();
        mFixedPos->copyTrans(&mString->mPosition);
    }

    mScaleController->updateNerve();
    mStampController->updateNerve();
    reactPointer();
    MR::reboundVelocityFromCollision(this, cNum_control_reboundVelocityFromCollision_1, cNum_control_reboundVelocityFromCollision_2, cNum_control_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, cNum_control_applyVelocityDampAndGravity_1, cNum_control_applyVelocityDampAndGravity_2, cNum_control_applyVelocityDampAndGravity_3, cNum_control_applyVelocityDampAndGravity_4, cNum_control_applyVelocityDampAndGravity_5);

    if (!isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) && !isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath)) &&
        !isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow))) {
        reactStringForce();
    }

    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvSearch)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvHit)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvAttack))) {
        startSwingLevelSound();
    }
}

Vec3f StringSpider::calcToRootVector() {
    return mString->mPosition - mPosition;
}

// Up along the string; front kept, or turned to the player (no turn when the player is straight above or below).
void StringSpider::calcPosture(f32 upRate, bool isFacePlayer) {
    Vec3f up;
    Vec3f toRoot = calcToRootVector();
    Vec3f front, toPlayer;
    mPoseQuat.getYDir(up);
    MR::normalizeOrZero(&toRoot);
    if (!MR::isNearZero(toRoot)) {
        mPoseQuat.getZDir(front);

        if (isFacePlayer) {
            MR::calcVecToPlayerH(&toPlayer, this, &up);
            MR::normalizeOrZero(&toPlayer);
            front.set(toPlayer);

            if (MR::isNearZero(front)) {
                return;
            }
        }

        MR::blendQuatUpFront(&mPoseQuat, toRoot, front, upRate, cNum_calcPosture_f1);
    }
}

void StringSpider::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "Wait");
    }

    calcPosture(cNum_exeWait_f1, false);
    f32 distance = MR::calcDistanceToPlayer(this);

    // Drops on switch A when it has one, else when the player comes near.
    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
        }
    } else if (distance < cNoticeDistance) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
    }
}

void StringSpider::endWait() {
    MR::invalidateClipping(this);
}

void StringSpider::exeDownStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DownStart");
    }

    calcPosture(cNum_exeDownStart_f1, false);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDown));
    }
}

void StringSpider::exeDownEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DownEnd");
    }

    calcPosture(cNum_exeDownEnd_f1, false);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
    }

    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_DOWN");
    calcPosture(cNum_exeDown_f1, true);
    extendString();
    f32 distance;
    bool isOver = isOverStringLength(nullptr, &distance);
    if ((mStringLengthMax - cNum_exeDown_f2) < distance) {
        MR::tryStartBck(this, "PreDownEnd");
    }

    if (mStringLength == mStringLengthMax && isOver) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDownEnd));
    }
}

void StringSpider::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Up");
    }

    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_UP");
    calcPosture(cNum_exeUp_f1, true);

    if (MR::calcDistanceToPlayer(this) < cNoticeDistance) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
    } else {
        shortenString(cNum_exeUp_f2);
        if (mStringLength == (cMinStringRate * mStringLengthMax)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvWait));
        }
    }
}

void StringSpider::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    calcPosture(cNum_exeSearch_f1, true);

    if (MR::isBindedWallOfMap(this) || MR::isBindedGround(this)) {
        shortenString(cNum_exeSearch_f2);
    } else {
        extendString();
    }

    // Swings harder within 800 and attacks after a while; goes back up beyond 1600 unless switch A holds it down.
    f32 distance = MR::calcDistanceToPlayer(this);
    if (distance < cNum_exeSearch_f3) {
        doSwingAction(cNum_exeSearch_f4, true);
        if (MR::isGreaterStep(this, getSearchToAttackTime())) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvAttack));
            return;
        }
    } else {
        doSwingAction(cNum_exeSearch_f5, true);
    }

    if ((!MR::isValidSwitchA(this) || !MR::isOnSwitchA(this)) && cNum_exeSearch_f6 < distance) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvUp));
    }
}

void StringSpider::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    calcPosture(cNum_exeAttack_f1, true);

    if (MR::isBindedWallOfMap(this) || MR::isBindedGround(this)) {
        shortenString(cNum_exeAttack_f2);
    } else {
        extendString();
    }

    doSwingAction(cNum_exeAttack_f3, false);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeHit() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hit");
        MR::startSound(this, "SE_EV_STRSPIDER_HIT");
    }

    calcPosture(cNum_exeHit_f1, true);
    extendString();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeSpinned() {
    if (MR::isFirstStep(this)) {
        MR::startBlowHitSound(this);
        MR::startBck(this, "Spin");
    }

    // Spins about its string, slowing to a stop over cSpinnedSteps; a free spider also circles around the string.
    f32 rate = MR::calcNerveRate(this, cSpinnedSteps);
    f32 rest = 1.0f - rate;
    if (mSwingSide == nullptr) {
        Vec3f toRoot = calcToRootVector();
        Vec3f circle;
        circle.cross(mGravity, toRoot);
        if (circle.dot(mVelocity) < 0.0f) {
            circle = -circle;
        }
        circle.setLength(cNum_exeSpinned_f1 * (rest));
        mVelocity.add(circle);
    }

    f32 volume = MR::getLinerValueFromMinMax(rest, 0.0f, 1.0f, cSpinMinVolume, cNum_exeSpinned_f2);
    f32 subLevel = MR::getLinerValueFromMinMax(rate, 0.0f, 1.0f, cSpinMinSubLevel, cNum_exeSpinned_f3);
    if (volume >= cNum_exeSpinned_f4) {
        MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SPIN", volume, subLevel, -1);
    }

    Quat4f spin;
    spin.setRotate(Vec3f(0, 1, 0), (-cNum_exeSpinned_f5 * rest));
    NebulaQuatMultiply(mPoseQuat, spin, mPoseQuat);
    calcPosture(cNum_exeSpinned_f6, false);

    if (MR::isGreaterStep(this, cSpinnedSteps)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeTrampleFall() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Press");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_EV_STRSPIDER_DEAD");
        MR::calcReflectionVector(&mVelocity, mGravity, cNum_exeTrampleFall_f1, 1.0f);
        MR::setBinderRadius(this, cNum_exeTrampleFall_setBinderRadius_1);
    }

    if (MR::isGreaterStep(this, cStep_exeTrampleFall_1)) {
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void StringSpider::exeWallHitDeath() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    if (MR::isGreaterStep(this, cStep_exeWallHitDeath_1)) {
        kill();
    }
}

void StringSpider::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Blow");
        MR::startSound(this, "SE_EV_STRSPIDER_DEAD");
        MR::startBlowHitSound(this);
        mString->kill();
    }

    mSpinHitController->execute(this);
}

void StringSpider::exePointerGripped() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Spin");
        if (!MR::tryStartStarPointerCommandStream(this, &mPosition, *MR::getStarPointerLastPointedPort(this), false)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
            return;
        }
    }

    // Dragged toward the pointer at an eighth of the distance (at most 25) while the button is held; dragged 100
    // past the string's length, it bursts into star pieces.
    s32 channel = *MR::getStarPointerLastPointedPort(this);
    if (!MR::isHeldJump(channel)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    } else {
        calcPosture(cNum_exePointerGripped_f1, false);
        Vec3f pointerPos;
        MR::calcStarPointerWorldPointingPosInsideEdge(&pointerPos, mPosition, channel);
        Vec3f drag(pointerPos);
        drag.sub(mPosition);
        drag.setLength(MR::clamp(cNum_exePointerGripped_f2 * drag.length(), 0.0f, cNum_exePointerGripped_f3));
        mVelocity.set(drag);
        f32 distance;
        isOverStringLength(nullptr, &distance);

        if ((cNum_exePointerGripped_f4 + mStringLength) < distance) {
            MR::appearStarPiece(this, mPosition, 3, cNum_exePointerGripped_f5, cNum_exePointerGripped_f6, false);
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
            MR::tryRumblePadStrong(this, channel);
        }
    }
}

void StringSpider::endPointerGripped() {
    tearDownPointerGripped();
}

void StringSpider::tearDownPointerGripped() {
    if (MR::isStarPointerCommandStream(this, *MR::getStarPointerLastPointedPort(this))) {
        MR::tryEndStarPointerCommandStream(this, *MR::getStarPointerLastPointedPort(this));
    }
}

void StringSpider::extendString() {
    mStringLength += cNum_extendString_f1;
    mStringLength = MR::clamp(mStringLength, cMinStringRate * mStringLengthMax, mStringLengthMax);
}

void StringSpider::shortenString(f32 length) {
    mStringLength -= length;
    mStringLength = MR::clamp(mStringLength, cMinStringRate * mStringLengthMax, mStringLengthMax);
}

void StringSpider::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    baseMtx.makeQuat(mPoseQuat);
    Pos3f stampMtx(mStampController->mMtx);
    baseMtx.concat(stampMtx, baseMtx);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    MR::setBaseScale(this, mScaleController->mScale * mScale);
    MR::copyJointPos(this, "Root", &mString->mRootPos);
    mString->calcAndSetBaseMtx();
}

void StringSpider::calcAnim() {
    LiveActor::calcAnim();
    mString->calcAnim();
}

void StringSpider::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvWallHitDeath)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath))) {
        return;
    }

    Vec3f away(pSender->mPosition);
    away.sub(pReceiver->mPosition);
    MR::normalizeOrZero(&away);
    if (MR::isSensorPlayer(pReceiver)) {
        if (isNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned))) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
            return;
        }

        if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, cNum_attackSensor_f1)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        // Hit the player: bounces back, 10 away along the floor and 13 up.
        MR::calcReflectionVector(&mVelocity, away, cNum_attackSensor_f2, 1.0f);
        Vec3f bounce(pSender->mPosition);
        bounce.sub(pReceiver->mPosition);
        MR::vecKillElement(bounce, mGravity, &bounce);
        MR::normalizeOrZero(&bounce);

        if (!MR::isNearZero(bounce)) {
            bounce.scale(cNum_attackSensor_f3);
            Vec3f up(mGravity);
            up.scale(cNum_attackSensor_f4);
            bounce.sub(up);
            mVelocity.add(bounce);
            setNerve(GET_NERVE(StringSpider, HostTypeNrvHit));
        }
    } else if (MR::isSensorEnemy(pReceiver) && MR::sendMsgPush(pReceiver, pSender)) {
        MR::calcReflectionVector(&mVelocity, away, cNum_attackSensor_f5, 1.0f);
        f32 speed = mVelocity.length();

        if (speed >= cNum_attackSensor_f6) {
            MR::startSound(this, "SE_EM_STRSPIDER_COLLISION", MR::getLinerValueFromMinMax(speed, 0.0f, cCollisionFullSpeed, 0.0f, cCollisionMaxVolume),
                           -1);
        }
    }
}

// Knocked away from the hitter along the floor; on a fixed swing line only the part along the line stays.
bool StringSpider::reactSpinHit(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f away(pReceiver->mPosition);
    away.sub(pSender->mPosition);
    MR::vecKillElement(away, mGravity, &away);
    MR::normalizeOrZero(&away);

    if (!MR::isNearZero(away)) {
        Vec3f side;
        side.cross(mGravity, away);
        side.setLength(cSpinHitSideSpeed);
        away.scale(cSpinHitSpeed);
        Vec3f up(mGravity);
        up.scale(cSpinHitUpSpeed);
        away.sub(up);

        if (mSwingSide != nullptr) {
            MR::vecKillElement(away, *mSwingSide, &away);
            MR::vecKillElement(side, *mSwingSide, &side);
        }

        mVelocity.set(away);
        mVelocity.add(side);
        return true;
    }

    return false;
}

bool StringSpider::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvWallHitDeath)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath))) {
        return false;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        return false;
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        // Takes the hitter's speed, away from it.
        Vec3f blow(pReceiver->mPosition);
        blow.sub(pSender->mPosition);
        blow.setLength(pSender->mHost->mVelocity.length());

        if (mSwingSide != nullptr) {
            MR::vecKillElement(blow, *mSwingSide, &blow);
        }

        mVelocity.add(blow);
        return true;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(StringSpider, HostTypeNrvBlow));
        return true;
    }

    return false;
}

bool StringSpider::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvWallHitDeath)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath))) {
        return false;
    }

    if (MR::isSensorPlayer(pSender)) {
        return false;
    }

    // Pushed out by half the overlap.
    Vec3f push(pReceiver->mPosition);
    push.sub(pSender->mPosition);
    f32 distance = push.length();
    push.setLength(cNum_receiveMsgPush_f1 * ((pReceiver->mRadius + pSender->mRadius) - distance));

    if (mSwingSide != nullptr) {
        MR::vecKillElement(push, *mSwingSide, &push);
    }

    mVelocity.add(push);
    return true;
}

bool StringSpider::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        if (reactSpinHit(pSender, pReceiver)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned));
        }

        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        if (isNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) ||
            isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow))) {
            return false;
        }

        return true;
    }

    if (isNerve(GET_NERVE(StringSpider, HostTypeNrvWallHitDeath)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall)) ||
        isNerve(GET_NERVE(StringSpider, HostTypeNrvBlow)) || isNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleDeath))) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerTrample(msg)) {
        Vec3f bounce(pSender->mPosition - pReceiver->mPosition);
        MR::vecKillElement(bounce, mGravity, &bounce);
        MR::normalizeOrZero(&bounce);

        if (!MR::isNearZero(bounce)) {
            bounce *= cNum_receiveMsgPlayerAttack_f1;
            bounce -= mGravity * cNum_receiveMsgPlayerAttack_f2;
            mVelocity += bounce;
        }

        setNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall));
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        if (mSwingSide != nullptr) {
            Vec3f fromHitter(pSender->mPosition - pReceiver->mPosition);
            MR::vecKillElement(fromHitter, *mSwingSide, &fromHitter);
            mSpinHitController->start(this, pReceiver->mPosition + fromHitter, pReceiver->mPosition);
        } else {
            mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        }

        setNerve(GET_NERVE(StringSpider, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (isNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned))) {
            if (mSwingSide != nullptr) {
                Vec3f fromHitter(pSender->mPosition - pReceiver->mPosition);
                MR::vecKillElement(fromHitter, *mSwingSide, &fromHitter);
                mSpinHitController->start(this, pReceiver->mPosition + fromHitter, pReceiver->mPosition);
            } else {
                mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
            }

            setNerve(GET_NERVE(StringSpider, HostTypeNrvBlow));
            return true;
        }

        if (reactSpinHit(pSender, pReceiver)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned));
        }

        return true;
    }

    return false;
}

bool StringSpider::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void StringSpider::startSwingLevelSound() {
    f32 speed = mVelocity.length();
    s32 volume1 = cNum_startSwingLevelSound_f1 * MR::getLinerValueFromMinMax(speed, cSwing1MinSpeed, cSwing1MaxSpeed, 0.0f, 1.0f);
    s32 volume2 = cNum_startSwingLevelSound_f2 * MR::getLinerValueFromMinMax(speed, cSwing2MinSpeed, cSwing2MaxSpeed, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SWING1", volume1);
    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SWING2", volume2);
}

StringSpiderString::StringSpiderString(const char* pName) : LiveActor(pName), mRotateQuat(0, 0, 0, 1), mRootPos(0, 0, 0) {
    lumacore::setClass(this, lumacore::kClassId< StringSpiderString >);
    initModelManagerWithAnm("StringSpiderString", nullptr, false);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_Enemy, MR::DrawType_None);
    MR::invalidateClipping(this);
}

// The string model points from its root (mPosition) to the spider's Root joint, stretched to that length.
void StringSpiderString::calcAndSetBaseMtx() {
    Vec3f scale(mScale);
    Vec3f toSpider(mPosition);
    toSpider.sub(mRootPos);
    MR::separateScalarAndDirection(&scale.y, &toSpider, toSpider);
    scale.y *= cNum_calcAndSetBaseMtx_f1;

    if (MR::isOppositeDirection(Vec3f(0, 1, 0), toSpider)) {
        mRotateQuat.setRotate(Vec3f(1, 0, 0), MR::pi());
    } else {
        mRotateQuat.setRotate(Vec3f(0, 1, 0), toSpider);
    }

    Pos3f baseMtx;
    baseMtx.makeQuat(mRotateQuat);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    MR::setBaseScale(this, scale);
}
