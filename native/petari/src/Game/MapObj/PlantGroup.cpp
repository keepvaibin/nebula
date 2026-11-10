#include "Game/MapObj/PlantGroup.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initMember_f1, "initMember.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_placeOnCollisionFormCircle_f1, "placeOnCollisionFormCircle.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_placeOnCollisionFormCircle_f2, "placeOnCollisionFormCircle.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_placeOnCollisionFormCircle_f3, "placeOnCollisionFormCircle.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_placeOnCollisionFormCircle_f4, "placeOnCollisionFormCircle.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoundingSphereRadius_f1, "calcBoundingSphereRadius.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoundingSphereRadius_f2, "calcBoundingSphereRadius.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoundingSphereRadius_f3, "calcBoundingSphereRadius.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoundingSphereRadius_f4, "calcBoundingSphereRadius.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBoundingSphereRadius_f5, "calcBoundingSphereRadius.f5", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_generateItem_f1, "generateItem.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_generateItem_f2, "generateItem.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPosture_f1, "initPosture.f1", 0.01f);

namespace {
    NEBULA_TUNABLE(s32, hIsShakableStep, 20);
    NEBULA_TUNABLE(f32, sSize, 160.0f);
    NEBULA_TUNABLE(f32, sCollisionRadius, 50.0f);
    NEBULA_TUNABLE(f32, sCheckLineLength, 1000.0f);
    NEBULA_TUNABLE(f32, sCoinVel, 30.0f);
    NEBULA_TUNABLE(f32, sPointerTouchRadius, 100.0f);
    NEBULA_TUNABLE(f32, sScratchVel, 6.0f);
    NEBULA_TUNABLE(s32, hGenerateItemTouch, 1);
    const s32 sHintEffectEmitInterval = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(f32, hShakeStartFrame, 3.0f);
    NEBULA_TUNABLE(f32, hWeakRate, 0.5f);
    NEBULA_TUNABLE(f32, hMiddleRate, 1.0f);
    NEBULA_TUNABLE(f32, hStrongRate, 1.5f);
    NEBULA_TUNABLE(f32, hStrongDist, 100.0f);
    NEBULA_TUNABLE(f32, hMiddleDist, 300.0f);
    NEBULA_TUNABLE(f32, hWeakDist, 500.0f);
};  // namespace

namespace NrvPlantMember {
    NEW_NERVE(HostTypeNrvWait, PlantMember, Wait);
    NEW_NERVE(HostTypeNrvHint, PlantMember, Hint);
    NEW_NERVE(HostTypeNrvShakeWeak, PlantMember, ShakeWeak);
    NEW_NERVE(HostTypeNrvShakeMiddle, PlantMember, ShakeMiddle);
    NEW_NERVE(HostTypeNrvShakeStrong, PlantMember, ShakeStrong);
};  // namespace NrvPlantMember

PlantGroup::PlantGroup(const char* pName)
    : LiveActor(pName), mMembers(), mMemberCount(7), mPlantType(), mIsStarPiece(), mHintPosition(0.0f), mHintRotation(0.0f),
      mHintTimer(::sHintEffectEmitInterval), mHintIndex() {
}

void PlantGroup::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
    MR::initDefaultPos(this, rIter);
    const char* name = nullptr;
    MR::getObjectName(&name, rIter);

    if (MR::isEqualString(name, "FlowerGroup")) {
        mPlantType = 1;
    } else if (MR::isEqualString(name, "FlowerBlueGroup")) {
        mPlantType = 2;
    } else {
        mPlantType = 0;
    }

    s32 count = -1;
    MR::getPlacementArg0NoInit(rIter, &count);

    if (count > 0) {
        mMemberCount = count;
    } else if (count == -1) {
        mMemberCount = 7;
    }

    s32 itemCount = 0;
    MR::getPlacementArg1NoInit(rIter, &itemCount);

    if (itemCount < 0) {
        itemCount = 0;
    }

    s32 itemType = -1;
    MR::getPlacementArg2NoInit(rIter, &itemType);

    if (itemType == 1) {
        mIsStarPiece = true;
        MR::declareStarPiece(this, itemCount);
    } else {
        mIsStarPiece = false;
        MR::declareCoin(this, itemCount);
    }

    initMember(itemCount, rIter);
    initSound(4, false);
    initEffectKeeper(0, "Bush");
    MR::setEffectHostSRT(this, "HintShakeLeaf", &mHintPosition, &mHintRotation, nullptr);
    MR::initStarPointerTarget(this, 0.0f, Vec3f(0.0f));
    MR::useStageSwitchSleep(this, rIter);
    MR::useStageSwitchReadAppear(this, rIter);

    if (MR::isValidSwitchAppear(this)) {
        makeActorDead();
        MR::syncStageSwitchAppear(this);
    } else {
        makeActorAppeared();
    }

    MR::tryRegisterDemoCast(this, rIter);
    mHintTimer = MR::getRandom(3, 10) * 10;
}

void PlantGroup::makeActorAppeared() {
    LiveActor::makeActorAppeared();

    for (s32 i = 0; i < mMemberCount; i++) {
        mMembers[i]->makeActorAppeared();
    }
}

void PlantGroup::makeActorDead() {
    LiveActor::makeActorDead();

    for (s32 i = 0; i < mMemberCount; i++) {
        mMembers[i]->makeActorDead();
    }
}

void PlantGroup::initMember(s32 itemCount, const TableIter&) {
    mMembers = new PlantMember*[mMemberCount];

    for (s32 i = 0; i < mMemberCount; i++) {
        if (mPlantType == 1) {
            mMembers[i] = new PlantMember("花", "Flower", false, nullptr);
        } else if (mPlantType == 2) {
            mMembers[i] = new PlantMember("青い花", "FlowerBlue", false, nullptr);
        } else {
            mMembers[i] = new PlantMember("草", "CutBush", true, nullptr);
        }

        mMembers[i]->initWithoutIter();
        MR::invalidateClipping(mMembers[i]);

        if (i < itemCount) {
            mMembers[i]->mHasItem = true;
        } else {
            mMembers[i]->mHasItem = false;
        }
    }

    for (s32 i = 0; i < mMemberCount; i++) {
        s32 index = MR::getRandom(0, i + 1);
        bool hasItem = mMembers[i]->mHasItem;
        mMembers[i]->mHasItem = mMembers[index]->mHasItem;
        mMembers[index]->mHasItem = hasItem;
    }

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "境界球", 16, cNum_initMember_f1, Vec3f(gZeroVec));
}

s32 PlantGroup::placeOnCollisionFormCircle(Vec3f* pCenter, const Vec3f& rGravity, const Vec3f& rAxisX, const Vec3f& rAxisY) {
    pCenter->set(0, 0, 0);
    s32 ring = 0;
    f32 angle = 0.0f;
    f32 angleStep = cNum_placeOnCollisionFormCircle_f1 * PI;
    f32 radius = ::sSize * (s16)ring;
    s32 placedCount = 0;

    for (s32 i = 0; i < mMemberCount; i++) {
        Vec3f offset(rAxisX);
        offset.scale(Angle::cosRadian(angle));
        offset += rAxisY * Angle::sinRadian(angle);
        offset.scale(radius);
        Vec3f start(mPosition);
        start += offset;
        const Vec3f up = rGravity * cNum_placeOnCollisionFormCircle_f2;
        const Vec3f* pUp = &up;
        start -= *pUp;

        const Vec3f ray = rGravity * ::sCheckLineLength;
        const Vec3f* pRay = &ray;

        if (MR::getFirstPolyOnLineToMap(&mMembers[i]->mPosition, nullptr, start, *pRay)) {
            *pCenter += mMembers[i]->mPosition;
            placedCount++;
        } else {
            mMembers[i]->kill();
        }

        mMembers[i]->initPosture();
        angle += angleStep;

        if (angle >= cNum_placeOnCollisionFormCircle_f3 * PI) {
            ring++;
            angle = 0.0f;
            angleStep = cNum_placeOnCollisionFormCircle_f4 * PI / (ring * 6);
            radius = ::sSize * ring;
        }
    }

    // No member found ground: the group's own place stands as the center.
    if (placedCount > 0) {
        pCenter->scale(1.0f / placedCount);
    } else {
        pCenter->set(mPosition);
    }

    return placedCount;
}

f32 PlantGroup::calcBoundingSphereRadius(const Vec3f& rCenter) {
    Vec3f min(rCenter);
    Vec3f max(rCenter);

    for (s32 i = 0; i < mMemberCount; i++) {
        if (!MR::isDead(mMembers[i])) {
            const Vec3f& pos = mMembers[i]->mPosition;

            if (pos.x < min.x) {
                min.x = pos.x;
            } else if (max.x < pos.x) {
                max.x = pos.x;
            }

            if (pos.y < min.y) {
                min.y = pos.y;
            } else if (max.y < pos.y) {
                max.y = pos.y;
            }

            if (pos.z < min.z) {
                min.z = pos.z;
            } else if (max.z < pos.z) {
                max.z = pos.z;
            }

            MR::isNoCalcAnim(mMembers[i]);
            mMembers[i]->calcAnim();
        }
    }

    mPosition.set((max + min) / cNum_calcBoundingSphereRadius_f1);
    Vec3f size(max - min);

    if (size.x < size.y) {
        if (size.y < size.z) {
            return size.z / cNum_calcBoundingSphereRadius_f2;
        }

        return size.y / cNum_calcBoundingSphereRadius_f3;
    }

    if (size.x < size.z) {
        return size.z / cNum_calcBoundingSphereRadius_f4;
    }

    return size.x / cNum_calcBoundingSphereRadius_f5;
}

void PlantGroup::initAfterPlacement() {
    Vec3f gravity;
    Vec3f axisX;
    axisX.set(1, 0, 0);
    Vec3f axisY;
    axisY.set(0, 1, 0);
    MR::calcGravityVector(this, mPosition, &gravity, nullptr, 0);
    MR::makeAxisCrossPlane(&axisX, &axisY, gravity);
    Vec3f center;
    placeOnCollisionFormCircle(&center, gravity, axisX, axisY);
    f32 radius = calcBoundingSphereRadius(center);
    f32 scale = mScale.y;
    HitSensor* sensor = getSensor("境界球");
    sensor->mRadius = ::sSize + radius * scale;
    MR::setStarPointerTargetRadius3d(this, ::sSize + radius * scale);
    MR::setClippingTypeSphere(this, ::sSize + radius * scale);
}

void PlantGroup::control() {
    bool pointing = MR::isStarPointerPointing2POnPressButton(this, nullptr, false, false);

    for (s32 i = 0; i < mMemberCount; i++) {
        mMembers[i]->animControl(this);
        mMembers[i]->movement();

        if (pointing) {
            Vec2f velocity(*MR::getStarPointerScreenVelocity(1));

            if (::sScratchVel * ::sScratchVel < velocity.squared()) {
                Vec3f pos(*MR::getStarPointerWorldPosUsingDepth(1));

                if (mMembers[i]->tryPush(pos, ::sPointerTouchRadius, 2)) {
                    MR::tryRumblePadVeryWeak(this, 1);
                    break;
                }
            }
        }
    }

    emitHintEffect();
}

void PlantGroup::emitHintEffect() {
    mHintTimer--;

    if (mHintTimer <= 0) {
        mHintTimer = ::sHintEffectEmitInterval;
        s32 index = (mMemberCount + mHintIndex) % mMemberCount;
        do {
            if (mMembers[index]->mHasItem && mMembers[index]->tryEmitHint()) {
                mHintIndex = (index + mMemberCount + 1) % mMemberCount;
                mHintPosition.set(mMembers[index]->mPosition);
                mHintRotation.set(mMembers[index]->mRotation);
                MR::emitEffect(this, "HintShakeLeaf");
                break;
            }

            index = (index + mMemberCount + 1) % mMemberCount;
        } while (index != mHintIndex);
    }
}

bool PlantGroup::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor*) {
    if (msg == ACTMES_TORNADO_ATTACK || msg == ACTMES_TORNADO_STORM_RANGE || msg == ACTMES_SPIN_STORM_RANGE) {
        for (s32 i = 0; i < mMemberCount; i++) {
            mMembers[i]->tryShake(pSender);
        }
    }

    return false;
}

void PlantGroup::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) || MR::isSensorEnemy(pReceiver)) {
        Vec3f velocity(pReceiver->mHost->mVelocity);
        MR::vecKillElement(velocity, pReceiver->mHost->mGravity, &velocity);

        if (velocity.squared() < cNum_attackSensor_f1) {
            return;
        }

        s32 touchType = 0;

        if (MR::isSensorPlayer(pReceiver) || MR::isSensorNpc(pReceiver)) {
            touchType = 1;
        } else if (MR::sendArbitraryMsg(ACTMES_PLANT_GROUP_EMIT_ITEM, pReceiver, pSender)) {
            touchType = 1;
        }

        for (s32 i = 0; i < mMemberCount; i++) {
            mMembers[i]->tryPush(pReceiver->mPosition, pReceiver->mRadius, touchType);
        }
    }
}

void PlantGroup::startClipped() {
    LiveActor::startClipped();

    for (s32 i = 0; i < mMemberCount; i++) {
        mMembers[i]->startClipped();
    }
}

void PlantGroup::endClipped() {
    LiveActor::endClipped();

    for (s32 i = 0; i < mMemberCount; i++) {
        mMembers[i]->endClipped();
    }
}

void PlantMember::init(const TableIter&) {
    mTouchType = 3;
    mHasItem = false;
    MR::onCalcAnim(this);
    initNerve(GET_NERVE(PlantMember, HostTypeNrvWait));
    appear();
}

bool PlantMember::tryEmitHint() {
    if (isNerve(GET_NERVE(PlantMember, HostTypeNrvWait))) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvHint));
        return true;
    }

    return false;
}

void PlantMember::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }
}

void PlantMember::exeHint() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HintShake");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvWait));
    }
}

void PlantMember::exeShakeWeak() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        MR::setBckFrame(this, ::hShakeStartFrame);
        MR::startSound(this, "SE_OJ_LEAVES_SWING");
        MR::setBckRate(this, ::hWeakRate);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvWait));
    }
}

void PlantMember::exeShakeMiddle() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        MR::setBckFrame(this, ::hShakeStartFrame);
        MR::startSound(this, "SE_OJ_LEAVES_SWING");
        MR::setBckRate(this, ::hMiddleRate);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvWait));
    }
}

void PlantMember::exeShakeStrong() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        MR::setBckFrame(this, ::hShakeStartFrame);
        MR::startSound(this, "SE_OJ_LEAVES_SWING");
        MR::setBckRate(this, ::hStrongRate);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvWait));
    }
}

bool PlantMember::generateItem(PlantGroup* pGroup) {
    if (mHasItem) {
        Vec3f velocity(-mGravity);
        velocity.scale(::sCoinVel);

        if (pGroup->mIsStarPiece) {
            MR::startSound(pGroup, "SE_OJ_STAR_PIECE_BURST");
            MR::appearStarPiece(pGroup, mPosition, 1, cNum_generateItem_f1, cNum_generateItem_f2, false);
        } else {
            MR::appearCoinPop(pGroup, mPosition, 1);
        }

        mHasItem = false;
        return true;
    }

    return false;
}

void PlantMember::initPosture() {
    MR::calcGravityVector(this, &mGravity, nullptr, 0);
    Vec3f axis;
    axis.set(1, 0, 0);

    if (MR::isSameDirection(mGravity, axis, cNum_initPosture_f1)) {
        axis.set(0, 1, 0);
    }

    Pos3f posture;
    MR::calcMtxFromGravityAndZAxis(&posture, this, mGravity, axis);
    Vec3f euler;
    Pos3f rotation;
    f32 angle = MR::getRandom(-PI, PI);
    rotation.makeRotate(mGravity, angle);
    posture.concat(rotation, posture);
    posture.getEuler(euler);
    f32 x = euler.x;
    f32 y = euler.y;
    f32 z = euler.z;
    x = _180_PI * x;
    y = _180_PI * y;
    z = _180_PI * z;
    mRotation.set(x, y, z);
    calcAnim();
}

bool PlantMember::tryShake(HitSensor* pSensor) {
    if (MR::isDead(this)) {
        return false;
    }

    if ((isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak)) || isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle)) ||
         isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong))) &&
        MR::isLessStep(this, ::hIsShakableStep)) {
        return false;
    }

    Vec3f diff(pSensor->mPosition - mPosition);
    MR::vecKillElement(diff, mGravity, &diff);
    f32 distance = diff.length();

    if (distance < ::hStrongDist) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong));
    } else if (distance < ::hMiddleDist) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle));
    } else if (distance < ::hWeakDist) {
        setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak));
    }

    return true;
}

bool PlantMember::tryPush(const Vec3f& rPosition, f32 radius, s32 touchType) {
    if (MR::isDead(this)) {
        return false;
    }

    if ((isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak)) || isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle)) ||
         isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong))) &&
        MR::isLessStep(this, ::hIsShakableStep)) {
        return false;
    }

    Vec3f diff(rPosition - mPosition);
    radius += ::sCollisionRadius * mScale.y;
    radius *= radius;

    if (diff.squared() < radius) {
        mTouchType = touchType;

        if (isNerve(GET_NERVE(PlantMember, HostTypeNrvWait)) || isNerve(GET_NERVE(PlantMember, HostTypeNrvHint))) {
            setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak));
        } else if (isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak))) {
            setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle));
        } else if (isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle))) {
            setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong));
        } else if (isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong))) {
            setNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong));
        }

        return true;
    }

    return false;
}

void PlantMember::animControl(PlantGroup* pGroup) {
    if (isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeWeak))) {
        if (mTouchType == ::hGenerateItemTouch) {
            generateItem(pGroup);
        }
    } else if (isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeMiddle)) || isNerve(GET_NERVE(PlantMember, HostTypeNrvShakeStrong))) {
        if (mTouchType == 1 || mTouchType == 2) {
            generateItem(pGroup);
        }
    }
}
