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
#include "Game/System/LumaCore.hpp"

namespace {
    // Swing sounds: volume 0 to 100 over these speeds.
    const f32 cSwing1MinSpeed = 2.0f;
    const f32 cSwing1MaxSpeed = 12.0f;
    const f32 cSwing2MinSpeed = 5.0f;
    const f32 cSwing2MaxSpeed = 25.0f;
    // Collision sound: volume 0 to cCollisionMaxVolume over speed 0 to cCollisionFullSpeed.
    const f32 cCollisionFullSpeed = 15.0f;
    const f32 cCollisionMaxVolume = 100.0f;
    // Spin sound: volume cSpinMinVolume..100 as the spin slows, sub level cSpinMinSubLevel..24.
    const f32 cSpinMinVolume = 40.0f;
    const f32 cSpinMinSubLevel = 5.0f;
    // A spin or star piece knocks the spider 30 away along the floor; the side and up parts are zero.
    const f32 cSpinHitSpeed = 30.0f;
    const f32 cSpinHitSideSpeed = -0.0f;
    const f32 cSpinHitUpSpeed = 0.0f;
    // The string never gets shorter than this share of its placed length (Obj_arg0).
    const f32 cMinStringRate = 0.333f;
    const s32 cSpinnedSteps = NebulaFrameClock::stepsOfSeconds(8);
    const f32 cNoticeDistance = 1400.0f;
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
    initBinder(80.0f * mScale.y, 0.0f, 0);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, mScale.y * 80.0f, Vec3f(0.0f, -13.0f, 13.0f));
    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(StringSpider, HostTypeNrvWait));
    MR::initStarPointerTarget(this, 80.0f, Vec3f(0, 0, 0));
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
        MR::initShadowVolumeCylinder(this, mScale.y * 80.0f);
        MR::setShadowDropLength(this, nullptr, shadowLength);
    } else {
        MR::initShadowVolumeSphere(this, mScale.y * 80.0f);
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
    upVec.setLength(200.0f);

    if (MR::getFirstPolyOnLineToMap(&rootPos, &triangle, mPosition - upVec * 0.5f, upVec)) {
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
            if (screenVel.length() < 20.0f) {
                return;
            }

            Vec3f flick;
            MR::getStarPointerWorldVelocityDirection(&flick, 1);
            flick.setLength(15.0f);
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
        if ((1000.0f + mStringLength) < distance) {
            kill();
        } else {
            MR::normalizeOrZero(&toRoot);
            if (!isNerve(GET_NERVE(StringSpider, HostTypeNrvPointerGripped))) {
                MR::calcReflectionVector(&mVelocity, toRoot, 0.25f, 1.0f);
            }

            f32 excess = distance - mStringLength;
            mVelocity += toRoot * excess * 0.1f;
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
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::applyVelocityDampAndGravity(this, 1.5f, 0.8f, 0.98f, 0.98f, 1.0f);

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

        MR::blendQuatUpFront(&mPoseQuat, toRoot, front, upRate, 0.03f);
    }
}

void StringSpider::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "Wait");
    }

    calcPosture(0.25f, false);
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

    calcPosture(0.25f, false);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDown));
    }
}

void StringSpider::exeDownEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DownEnd");
    }

    calcPosture(0.25f, false);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
    }

    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_DOWN");
    calcPosture(0.25f, true);
    extendString();
    f32 distance;
    bool isOver = isOverStringLength(nullptr, &distance);
    if ((mStringLengthMax - 230.0f) < distance) {
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
    calcPosture(0.25f, true);

    if (MR::calcDistanceToPlayer(this) < cNoticeDistance) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
    } else {
        shortenString(5.0f);
        if (mStringLength == (cMinStringRate * mStringLengthMax)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvWait));
        }
    }
}

void StringSpider::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    calcPosture(0.25f, true);

    if (MR::isBindedWallOfMap(this) || MR::isBindedGround(this)) {
        shortenString(40.0f);
    } else {
        extendString();
    }

    // Swings harder within 800 and attacks after a while; goes back up beyond 1600 unless switch A holds it down.
    f32 distance = MR::calcDistanceToPlayer(this);
    if (distance < 800.0f) {
        doSwingAction(0.2f, true);
        if (MR::isGreaterStep(this, getSearchToAttackTime())) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvAttack));
            return;
        }
    } else {
        doSwingAction(0.05f, true);
    }

    if ((!MR::isValidSwitchA(this) || !MR::isOnSwitchA(this)) && 1600.0f < distance) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvUp));
    }
}

void StringSpider::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    calcPosture(0.25f, true);

    if (MR::isBindedWallOfMap(this) || MR::isBindedGround(this)) {
        shortenString(40.0f);
    } else {
        extendString();
    }

    doSwingAction(0.8f, false);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeHit() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hit");
        MR::startSound(this, "SE_EV_STRSPIDER_HIT");
    }

    calcPosture(0.25f, true);
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
        circle.setLength(0.3f * (rest));
        mVelocity.add(circle);
    }

    f32 volume = MR::getLinerValueFromMinMax(rest, 0.0f, 1.0f, cSpinMinVolume, 100.0f);
    f32 subLevel = MR::getLinerValueFromMinMax(rate, 0.0f, 1.0f, cSpinMinSubLevel, 24.0f);
    if (volume >= 51.0f) {
        MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SPIN", volume, subLevel, -1);
    }

    Quat4f spin;
    spin.setRotate(Vec3f(0, 1, 0), (-0.6f * rest));
    NebulaQuatMultiply(mPoseQuat, spin, mPoseQuat);
    calcPosture(0.25f, false);

    if (MR::isGreaterStep(this, cSpinnedSteps)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    }
}

void StringSpider::exeTrampleFall() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Press");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_EV_STRSPIDER_DEAD");
        MR::calcReflectionVector(&mVelocity, mGravity, 0.25f, 1.0f);
        MR::setBinderRadius(this, 10.0f);
    }

    if (MR::isGreaterStep(this, 5)) {
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void StringSpider::exeWallHitDeath() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    if (MR::isGreaterStep(this, 35)) {
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
        calcPosture(0.25f, false);
        Vec3f pointerPos;
        MR::calcStarPointerWorldPointingPosInsideEdge(&pointerPos, mPosition, channel);
        Vec3f drag(pointerPos);
        drag.sub(mPosition);
        drag.setLength(MR::clamp(0.125f * drag.length(), 0.0f, 25.0f));
        mVelocity.set(drag);
        f32 distance;
        isOverStringLength(nullptr, &distance);

        if ((100.0f + mStringLength) < distance) {
            MR::appearStarPiece(this, mPosition, 3, 10.0f, 40.0f, false);
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
    mStringLength += 100.0f;
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

        if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, 0.25f)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        // Hit the player: bounces back, 10 away along the floor and 13 up.
        MR::calcReflectionVector(&mVelocity, away, 0.25f, 1.0f);
        Vec3f bounce(pSender->mPosition);
        bounce.sub(pReceiver->mPosition);
        MR::vecKillElement(bounce, mGravity, &bounce);
        MR::normalizeOrZero(&bounce);

        if (!MR::isNearZero(bounce)) {
            bounce.scale(10.0f);
            Vec3f up(mGravity);
            up.scale(13.0f);
            bounce.sub(up);
            mVelocity.add(bounce);
            setNerve(GET_NERVE(StringSpider, HostTypeNrvHit));
        }
    } else if (MR::isSensorEnemy(pReceiver) && MR::sendMsgPush(pReceiver, pSender)) {
        MR::calcReflectionVector(&mVelocity, away, 0.25f, 1.0f);
        f32 speed = mVelocity.length();

        if (speed >= 3.0f) {
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
    push.setLength(0.5f * ((pReceiver->mRadius + pSender->mRadius) - distance));

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
            bounce *= 10.0f;
            bounce -= mGravity * 13.0f;
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
    s32 volume1 = 100.0f * MR::getLinerValueFromMinMax(speed, cSwing1MinSpeed, cSwing1MaxSpeed, 0.0f, 1.0f);
    s32 volume2 = 100.0f * MR::getLinerValueFromMinMax(speed, cSwing2MinSpeed, cSwing2MaxSpeed, 0.0f, 1.0f);
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
    scale.y *= 0.063f;

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
