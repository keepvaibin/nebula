#include "Game/Boss/BossStinkBugFunction.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <nebula_geometry.hpp>

void BossStinkBugFunction::addHeightOffset(Vec3f* pOut, const BossStinkBug* pStinkBug, f32 height) {
    pOut->add(pStinkBug->mGravity * -height);
}

void BossStinkBugFunction::addLocalHeightOffset(Vec3f* pOut, BossStinkBug* pStinkBug, f32 height) {
    if (!MR::isNearZero(height)) {
        pStinkBug->updatePose();
        Vec3f upVec;
        pStinkBug->calcUpVec(&upVec);
        pOut->scaleAdd(height, upVec, *pOut);
    }
}

void BossStinkBugFunction::calcDiffCurrentRailPosition(Vec3f* pOut, BossStinkBug* pStinkBug, f32 heightOffset, f32 localHeightOffset) {
    Vec3f railPosition;
    railPosition.set(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPosition, pStinkBug, heightOffset);
    addLocalHeightOffset(&railPosition, pStinkBug, localHeightOffset);
    pOut->set(pStinkBug->mPosition - railPosition);
}

void BossStinkBugFunction::moveRail(BossStinkBug* pStinkBug, f32 speed, f32 heightOffset, f32 localHeightOffset) {
    MR::moveCoord(pStinkBug, speed);
    Vec3f railPos(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPos, pStinkBug, heightOffset);
    MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, MR::getRailDirection(pStinkBug), 10.0f);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    pStinkBug->mPosition.set(railPos);
}

void BossStinkBugFunction::turnRailGround(BossStinkBug* pStinkBug, const Vec3f& rTurnFront, f32 rate, bool isRailToEnd) {
    if (MR::isRailGoingToEnd(pStinkBug) == isRailToEnd) {
        MR::reverseRailDirection(pStinkBug);
    }
    MR::rotateVecDegree(&pStinkBug->mFront, rTurnFront, pStinkBug->mGravity,
                        MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f) * 180.0f * -(isRailToEnd ? 1.0f : -1.0f));
}

void BossStinkBugFunction::turnRail(BossStinkBug* pStinkBug, const Vec3f& rTurnFront, f32 baseRoll, f32 rate, f32 speed, f32 heightOffset,
                                    f32 localHeightOffset, bool isRailToEnd) {
    f32 moveCoordParameter;

    // Slow to a stop over the first half, turn back on the rail, speed up over the second.
    if (rate <= 0.5f) {
        moveCoordParameter = speed * MR::getEaseInOutValue(1.0f - MR::normalize(rate, 0.0f, 0.5f), 0.0f, 1.0f, 1.0f);
    } else {
        if (MR::isRailGoingToEnd(pStinkBug) == isRailToEnd) {
            MR::reverseRailDirection(pStinkBug);
        }
        moveCoordParameter = speed * MR::getEaseInOutValue(MR::normalize(rate, 0.5f, 1.0f), 0.0f, 1.0f, 1.0f);
    }

    MR::moveCoord(pStinkBug, moveCoordParameter);
    Vec3f railPos(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPos, pStinkBug, heightOffset);

    f32 turnSign = (isRailToEnd ? 1.0f : -1.0f);
    f32 bend = 4.0f * rate * (rate - 1.0f);
    f32 bendSq = bend * bend;
    f32 rollAngle = baseRoll * (1.0f - bendSq) + (90.0f * turnSign) * bendSq;
    pStinkBug->mRotation.z = MR::repeat(rollAngle, -180.0f, 360.0f);

    MR::rotateVecDegree(&pStinkBug->mFront, rTurnFront, pStinkBug->mGravity, MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f) * 180.0f * -turnSign);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    pStinkBug->mPosition.set(railPos);
}

void BossStinkBugFunction::regainToRail(BossStinkBug* pStinkBug, const Vec3f& rRailDiff, f32 rate, f32 speed, f32 heightOffset,
                                        f32 localHeightOffset) {
    pStinkBug->mRotation.z *= 0.9f;
    MR::moveCoord(pStinkBug, speed);
    Vec3f railPos(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPos, pStinkBug, heightOffset);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    Vec3f dif = railPos - pStinkBug->mPosition;
    if (MR::isNearZero(dif)) {
        MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, dif, 2.0f);
    } else {
        MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, MR::getRailDirection(pStinkBug), 10.0f);
    }
    pStinkBug->mPosition.scaleAdd(1.0f - rate, rRailDiff, railPos);
}

void BossStinkBugFunction::setFallVelocity(BossStinkBug* pStinkBug, const Vec3f& rBase, f32 rate, f32 peakRate, f32 height) {
    f32 scaleAddFactor;

    if (rate < peakRate) {
        f32 rise = 1.0f / peakRate * (rate - peakRate);
        rise *= rise;
        rise *= rise;
        scaleAddFactor = 1.0f - rise;
    } else {
        scaleAddFactor = 1.0f - MR::getEaseInOutValue(1.0f / (1.0f - peakRate) * (rate - peakRate), 0.0f, 1.0f, 1.0f);
    }

    Vec3f target;
    target.scaleAdd(scaleAddFactor * height, pStinkBug->mGravity, rBase);

    pStinkBug->mVelocity.set(target - pStinkBug->mPosition);
}

void BossStinkBugFunction::throwBomb(BossStinkBug* pStinkBug) {
    pStinkBug->throwBomb(20.0f, 10.0f);
}

void BossStinkBugFunction::invalidateAttack(BossStinkBug* pStinkBug) {
    MR::invalidateHitSensor(pStinkBug, "attack_left");
    MR::invalidateHitSensor(pStinkBug, "attack_right");
}

bool BossStinkBugFunction::isHipDropableSensor(const BossStinkBug* pStinkBug, const HitSensor* pSensor) {
    return pStinkBug->isSensorBody(pSensor);
}

bool BossStinkBugFunction::isExistPlayerBack(const BossStinkBug* pStinkBug, f32 distance) {
    return MR::calcDifferenceRailCoord(pStinkBug, MR::calcNearestRailCoord(pStinkBug, *MR::getPlayerPos())) < -distance;
}

void BossStinkBugFunction::validateAttack(BossStinkBug* pStinkBug) {
    MR::validateHitSensor(pStinkBug, "attack_left");
    MR::validateHitSensor(pStinkBug, "attack_right");
}
