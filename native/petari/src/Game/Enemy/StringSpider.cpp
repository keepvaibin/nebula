#include "Game/Enemy/StringSpider.hpp"
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

namespace {
    // hPointerPunchAddVel
    // hPointerPunchMinScVel
    // hPunchInvalidTime
    // hCheckMapLength
    // hCommonGravity
    // hGroundDamp
    // hAirDampH
    // hAirDampV
    // hWatchDistance
    // hReturnHeightLength
    // hTurnFrontRate
    // hStringReboundRate
    // hStringReboundMinSpeed
    // hStringForce
    // hStringMinMaxRate
    // hStringAddLength
    // hStringSubLength
    // hStringSubLengthOnBind
    // hDeathWallHitVel
    // hAttackHitVel
    // hDeathStringExpandLength
    // hPunchHitVelH
    // hSpinnedTime
    // hSpinnedSpinTime
    // hSpinnedRotateAngle
    // hSpinnedAddSideVel
    // hKickableTime
    // hHitTime
    // hAttackHitVelV
    // hAttackHitVelH
    // hUpdatePostureRate
    // hModelStringScaleMul
    // hExplodeHitVel
    // hSearchToAttackTime
    // hSearchToAttackTimeFix
    // hAttackTime
    // hSwingAttackVel
    // hSideSwingDistance
    // hIsStableVel
    // hSwingVel
    // hSwingVelSlow
    // hPushRate
    // hWallHitDeathTime
    // hTrampleDeathTime
    // hPointerTouchVel
    // hPointerGrippedMulVel
    // hPointerGrippedAddVelMax
    // hPointerGrippedLimitAddLength
    // hSpinRangeAddForce
    // hStarPieceAttackAddVel
    // hDownAnimStartSubLength
    static const f32 hSwing1SeMinSpeed = 2.0f;
    static const f32 hSwing1SeMaxSpeed = 12.0f;
    static const f32 hSwing1SeMaxVolume = 0.0f;
    static const f32 hSwing2SeMinSpeed = 5.0f;
    static const f32 hSwing2SeMaxSpeed = 25.0f;
    static const f32 hSwing2SeMaxVolume = 0.0f;
    static const f32 hCollisionSePlaySpeed = 15.0f;
    static const f32 hCollisionSeMaxVolumeSpeed = 100.0f;
    static const f32 hSpinSeMinVolume = 5.0f;
    static const f32 hSpinSeMinNoteLength = 0.0f;
    static const f32 hSpinSeMaxNoteLength = 1.0f;
    static const f32 hSpinSeLimitMinVolume = 40.0f;
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
      mPoseQuat(0, 0, 0, 1), mIsPointerPulled(false), mStringLengthMax(300.0f), mStringLength(300.0f), mPointerStep(0), mSwingFront(nullptr), mSwingSide(nullptr) {
}

s32 StringSpider::getSearchToAttackTime() {
    if (mSwingFront == nullptr) {
        return 300;
    }

    return 60;
}

void StringSpider::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);

        s32 arg = -1;
        MR::getPlacementArg0NoInit(rIter, &arg);
        if (arg != -1) {
            f32 fArg = arg;
            mStringLength = fArg * 0.333f;
            mStringLengthMax = fArg;
        }

        arg = -1;
        MR::getPlacementArg1NoInit(rIter, &arg);
        if (arg == 2) {
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
    f32 yScale = mScale.y;
    initBinder(80.0f * mScale.y, 0.0f, 0);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 32, yScale * 80.0f, Vec3f(0.0f, -13.0f, 13.0f));
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

    s32 arg2 = -1;
    MR::getPlacementArg2NoInit(rIter, &arg2);
    if (arg2 > 0) {
        MR::initShadowVolumeCylinder(this, mScale.y * 80.0f);
        MR::setShadowDropLength(this, nullptr, arg2);
    } else {
        MR::initShadowVolumeSphere(this, mScale.y * 80.0f);
    }

    mFixedPos = new FixedPosition((MtxPtr) nullptr, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    MR::setGroupClipping(this, rIter, 16);
    mString = new StringSpiderString("ストリングスパイダーの糸");
    MR::resetPosition(mString, mPosition);
    mString->makeActorAppeared();
    makeActorAppeared();
    MR::emitEffect(this, "EyeLight");
}

void StringSpider::initAfterPlacement() {
    Triangle triangle = Triangle();
    Vec3f v1, upVec, v2;
    MR::calcUpVec(&upVec, this);
    upVec.setLength(200.0f);

    if (MR::getFirstPolyOnLineToMap(&v1, &triangle, mPosition - upVec * 0.5f, upVec)) {
        triangle.getBaseInvMtx()->mult(v1, v2);
        MR::resetPosition(mString, v1);
        mFixedPos->setBaseMtx(triangle.getBaseMtx()->mMtx);
        mFixedPos->setLocalTrans(v2);
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

void StringSpider::doSwingAction(f32 a1, bool a2) {
    Vec3f stringPos(mString->mPosition);
    Vec3f v1, v3;
    stringPos.sub(mPosition);
    MR::normalizeOrZero(&stringPos);
    if (!MR::isOppositeDirection(mGravity, stringPos, 1.0f)) {
        return;
    }

    if (a2) {
        if (mSwingFront != nullptr) {
            return;
        }
        mPoseQuat.getXDir(v1);
    } else {
        if (mSwingFront == nullptr) {
            mPoseQuat.getZDir(v1);
        } else {
            v1.set(*mSwingFront);
        }

        if (MR::isFirstStep(this)) {
            MR::startBck(this, "PreAttack");
        }

        if (MR::isStep(this, 30)) {
            MR::startBck(this, "Attack");
        }

        if (MR::isLessStep(this, 30)) {
            v1 = -v1;
        }
    }

    MR::vecKillElement(mVelocity, stringPos, &v3);
    if (!a2 || MR::isNearZero(v3)) {
        v3.set(v1);
    } else {
        v1.scale(v1.dot(v3));
        MR::normalizeOrZero(&v1);
        v3.set(v1);
    }

    mVelocity += v3 * a1;
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

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        if (!mIsPointerPulled && mPointerStep == 0) {
            Vec2f screenVel = *MR::getStarPointerScreenVelocity(1);
            if (screenVel.length() < 20.0f) {
                return;
            }

            Vec3f a1;
            MR::getStarPointerWorldVelocityDirection(&a1, 1);
            a1.setLength(15.0f);
            mVelocity.add(a1);
            mIsPointerPulled = true;
            Vec3f a2(mPosition);
            f32 radius = getSensor("body")->mRadius;
            a1.setLength(radius);
            a2.add(a1);
            MR::emitEffectHit(this, a2, nullptr);
            mPointerStep = 30;
            MR::start2PAttackAssistSound();
            setNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned));
        }
        mIsPointerPulled = true;
    } else {
        mIsPointerPulled = false;
    }
}

void StringSpider::reactStringForce() {
    f32 v1;
    Vec3f v2;
    if (isOverStringLength(&v2, &v1)) {
        if ((1000.0f + mStringLength) < v1) {
            kill();
        } else {
            MR::normalizeOrZero(&v2);
            if (!isNerve(GET_NERVE(StringSpider, HostTypeNrvPointerGripped))) {
                MR::calcReflectionVector(&mVelocity, v2, 0.25f, 1.0f);
            }

            f32 v4 = v1 - mStringLength;
            mVelocity += v2 * v4 * 0.1f;
        }
    }
}

bool StringSpider::isOverStringLength(Vec3f* a1, f32* a2) {
    Vec3f rootVec = calcToRootVector();
    f32 rootLen = rootVec.length();
    if (a1 != nullptr) {
        a1->set(rootVec);
    }

    if (a2 != nullptr) {
        *a2 = rootLen;
    }

    return mStringLength < rootLen;
}

void StringSpider::control() {
    if (mFixedPos != nullptr) {
        mFixedPos->calc();
        mFixedPos->copyTrans(&mString->mPosition);
    }

    mScaleController->updateNerve();
    mStampController->updateNerve();
    reactPointer();

    if (!isNerve(GET_NERVE(StringSpider, HostTypeNrvPointerGripped))) {
        if (MR::isBindedWallOfMap(this)) {
            Vec3f v9(*MR::getWallNormal(this));
            Vec3f v8(*MR::getWallHitPos(this));
        } else if (MR::isBindedRoof(this)) {
            Vec3f v7(*MR::getRoofNormal(this));
            Vec3f v6(*MR::getRoofHitPos(this));
        }
    }

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

void StringSpider::calcPosture(f32 a1, bool a2) {
    Vec3f v1;
    Vec3f rootVec = calcToRootVector();
    Vec3f v3, v9;
    mPoseQuat.getYDir(v1);
    MR::normalizeOrZero(&rootVec);
    if (!MR::isNearZero(rootVec)) {
        mPoseQuat.getZDir(v3);

        if (a2) {
            MR::calcVecToPlayerH(&v9, this, &v1);
            MR::normalizeOrZero(&v9);
            v3.set(v9);

            if (!MR::isNearZero(v3) == false) {
                return;
            }
        }

        MR::blendQuatUpFront(&mPoseQuat, rootVec, v3, a1, 0.03f);
    }
}

void StringSpider::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "Wait");
    }

    calcPosture(0.25f, false);
    f32 distance = MR::calcDistanceToPlayer(this);

    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
        }
    } else if (distance < 1400.0f) {
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
    MR::calcDistanceToPlayer(this);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDown));
    }
}

void StringSpider::exeDownEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DownEnd");
    }

    calcPosture(0.25f, false);
    MR::calcDistanceToPlayer(this);

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
    f32 v1;
    bool isOver = isOverStringLength(nullptr, &v1);
    if ((mStringLengthMax - 230.0f) < v1) {
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

    if (MR::calcDistanceToPlayer(this) < 1400.0f) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvDownStart));
    } else {
        shortenString(5.0f);
        if (mStringLength == (0.333f * mStringLengthMax)) {
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

    f32 calc = MR::calcDistanceToPlayer(this);
    if (calc < 800.0f) {
        doSwingAction(0.2f, true);
        if (MR::isGreaterStep(this, getSearchToAttackTime())) {
            setNerve(GET_NERVE(StringSpider, HostTypeNrvAttack));
            return;
        }
    } else {
        doSwingAction(0.05f, true);
    }

    if ((!MR::isValidSwitchA(this) || !MR::isOnSwitchA(this)) && 1600.0f < calc) {
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

    if (MR::isGreaterStep(this, 60)) {
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

    f32 nerveRate = MR::calcNerveRate(this, 480);
    f32 nerveRateMinus = 1.0f - nerveRate;
    if (mSwingSide == nullptr) {
        Vec3f rootVec = calcToRootVector();
        Vec3f v15;
        v15.cross(mGravity, rootVec);
        if (v15.dot(mVelocity) < 0.0f) {
            v15 = -v15;
        }
        v15.setLength(0.3f * (nerveRateMinus));
        mVelocity.add(v15);
    }

    f32 liner1 = MR::getLinerValueFromMinMax(nerveRateMinus, ::hSpinSeMinNoteLength, ::hSpinSeMaxNoteLength, ::hSpinSeLimitMinVolume, 100.0f);
    f32 liner2 = MR::getLinerValueFromMinMax(nerveRate, ::hSpinSeMinNoteLength, ::hSpinSeMaxNoteLength, ::hSpinSeMinVolume, 24.0f);
    if (liner1 >= 51.0f) {
        MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SPIN", liner1, liner2, -1);
    }

    Quat4f v14;
    v14.setRotate(Vec3f(0, 1, 0), (-0.6f * nerveRateMinus));
    NebulaQuatMultiply(mPoseQuat, v14, mPoseQuat);
    calcPosture(0.25f, false);

    if (MR::isGreaterStep(this, 480)) {
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

    s32 channel = PLAYER_1;
    if (!MR::isHeldJump(channel = *MR::getStarPointerLastPointedPort(this))) {
        setNerve(GET_NERVE(StringSpider, HostTypeNrvSearch));
    } else {
        calcPosture(0.25f, false);
        Vec3f v8;
        MR::calcStarPointerWorldPointingPosInsideEdge(&v8, mPosition, channel);
        Vec3f v7(v8);
        v7.sub(mPosition);
        v7.setLength(MR::clamp(0.125f * v7.length(), 0.0f, 25.0f));
        mVelocity.set(v7);
        f32 v9;
        isOverStringLength(nullptr, &v9);

        if ((100.0f + mStringLength) < v9) {
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
    mStringLength = MR::clamp(mStringLength, 0.333f * mStringLengthMax, mStringLengthMax);
}

void StringSpider::shortenString(f32 a1) {
    mStringLength -= a1;
    mStringLength = MR::clamp(mStringLength, 0.333f * mStringLengthMax, mStringLengthMax);
}

void StringSpider::calcAndSetBaseMtx() {
    Pos3f v14;
    v14.makeQuat(mPoseQuat);
    Pos3f v13(mStampController->mMtx);
    v14.concat(v13, v14);
    v14.setTrans(mPosition);
    MR::setBaseTRMtx(this, v14);
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

    Vec3f v10(pSender->mPosition);
    v10.sub(pReceiver->mPosition);
    MR::normalizeOrZero(&v10);
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

        MR::calcReflectionVector(&mVelocity, v10, 0.25f, 1.0f);
        Vec3f v9(pSender->mPosition);
        v9.sub(pReceiver->mPosition);
        MR::vecKillElement(v9, mGravity, &v9);
        MR::normalizeOrZero(&v9);

        if (!MR::isNearZero(v9)) {
            v9.scale(10.0f);
            Vec3f v8(mGravity);
            v8.scale(13.0f);
            v9.sub(v8);
            mVelocity.add(v9);
            setNerve(GET_NERVE(StringSpider, HostTypeNrvHit));
        }
    } else if (MR::isSensorEnemy(pReceiver) && MR::sendMsgPush(pReceiver, pSender)) {
        MR::calcReflectionVector(&mVelocity, v10, 0.25f, 1.0f);
        f32 len = mVelocity.length();

        if (len >= 3.0f) {
            MR::startSound(this, "SE_EM_STRSPIDER_COLLISION",
                           MR::getLinerValueFromMinMax(len, 0.0f, ::hCollisionSePlaySpeed, 0.0f, ::hCollisionSeMaxVolumeSpeed), -1);
        }
    }
}

bool StringSpider::reactSpinHit(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f v10(pReceiver->mPosition);
    v10.sub(pSender->mPosition);
    MR::vecKillElement(v10, mGravity, &v10);
    MR::normalizeOrZero(&v10);

    if (!MR::isNearZero(v10)) {
        Vec3f v9;
        v9.cross(mGravity, v10);
        v9.setLength(-0.0f);
        v10.scale(30.0f);
        Vec3f v8(mGravity);
        v8.scale(0.0f);
        v10.sub(v8);

        if (mSwingSide != nullptr) {
            MR::vecKillElement(v10, *mSwingSide, &v10);
            MR::vecKillElement(v9, *mSwingSide, &v9);
        }

        mVelocity.set(v10);
        mVelocity.add(v9);
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
        Vec3f v11(pReceiver->mPosition);
        v11.sub(pSender->mPosition);
        v11.setLength(pSender->mHost->mVelocity.length());

        if (mSwingSide != nullptr) {
            MR::vecKillElement(v11, *mSwingSide, &v11);
        }

        mVelocity.add(v11);
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
    } else {
        Vec3f v14(pReceiver->mPosition);
        v14.sub(pSender->mPosition);
        f32 rad2 = pSender->mRadius;
        f32 rad1 = pReceiver->mRadius;
        f32 len = v14.length();
        v14.setLength(0.5f * ((rad1 + rad2) - len));

        if (mSwingSide != nullptr) {
            MR::vecKillElement(v14, *mSwingSide, &v14);
        }

        mVelocity.add(v14);
        return true;
    }
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
        Vec3f v16(pSender->mPosition - pReceiver->mPosition);
        MR::vecKillElement(v16, mGravity, &v16);
        MR::normalizeOrZero(&v16);

        if (!MR::isNearZero(v16)) {
            v16 *= 10.0f;
            v16 -= mGravity * 13.0f;
            mVelocity += v16;
        }

        setNerve(GET_NERVE(StringSpider, HostTypeNrvTrampleFall));
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        if (mSwingSide != nullptr) {
            Vec3f v15(pSender->mPosition - pReceiver->mPosition);
            MR::vecKillElement(v15, *mSwingSide, &v15);
            mSpinHitController->start(this, pReceiver->mPosition + v15, pReceiver->mPosition);
        } else {
            mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        }

        setNerve(GET_NERVE(StringSpider, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (isNerve(GET_NERVE(StringSpider, HostTypeNrvSpinned))) {
            if (mSwingSide != nullptr) {
                Vec3f v14(pSender->mPosition - pReceiver->mPosition);
                MR::vecKillElement(v14, *mSwingSide, &v14);
                mSpinHitController->start(this, pReceiver->mPosition + v14, pReceiver->mPosition);
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

bool StringSpider::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return false;
}

void StringSpider::startSwingLevelSound() {
    f32 mag = mVelocity.length();
    s32 liner1 = 100.0f * MR::getLinerValueFromMinMax(mag, ::hSwing1SeMinSpeed, ::hSwing1SeMaxSpeed, ::hSwing1SeMaxVolume, 1.0f);
    s32 liner2 = 100.0f * MR::getLinerValueFromMinMax(mag, ::hSwing2SeMinSpeed, ::hSwing2SeMaxSpeed, ::hSwing2SeMaxVolume, 1.0f);
    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SWING1", liner1);
    MR::startLevelSound(this, "SE_EM_LV_STRSPIDER_SWING2", liner2);
}

StringSpiderString::StringSpiderString(const char* pName) : LiveActor(pName), mRotateQuat(0, 0, 0, 1), mRootPos(0, 0, 0) {
    initModelManagerWithAnm("StringSpiderString", nullptr, false);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_Enemy, MR::DrawType_None);
    MR::invalidateClipping(this);
}

void StringSpiderString::calcAndSetBaseMtx() {
    Vec3f v10(mScale);
    Vec3f v9(mPosition);
    v9.sub(mRootPos);
    MR::separateScalarAndDirection(&v10.y, &v9, v9);
    v10.y *= 0.063f;

    if (MR::isOppositeDirection(Vec3f(0, 1, 0), v9)) {
        mRotateQuat.setRotate(Vec3f(1, 0, 0), MR::pi());
    } else {
        mRotateQuat.setRotate(Vec3f(0, 1, 0), v9);
    }

    Pos3f v12;
    v12.makeQuat(mRotateQuat);
    v12.setTrans(mPosition);
    MR::setBaseTRMtx(this, v12);
    MR::setBaseScale(this, v10);
}
