#include "Game/Boss/BossStinkBugFunction.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_moveRail_f1, "moveRail.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRailGround_f1, "turnRailGround.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f1, "turnRail.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f2, "turnRail.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f3, "turnRail.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f4, "turnRail.f4", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f5, "turnRail.f5", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f6, "turnRail.f6", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f7, "turnRail.f7", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnRail_f8, "turnRail.f8", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_regainToRail_f1, "regainToRail.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_regainToRail_f2, "regainToRail.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_regainToRail_f3, "regainToRail.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwBomb_f1, "throwBomb.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwBomb_f2, "throwBomb.f2", 10.0f);


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
    MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, MR::getRailDirection(pStinkBug), cNum_moveRail_f1);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    pStinkBug->mPosition.set(railPos);
}

void BossStinkBugFunction::turnRailGround(BossStinkBug* pStinkBug, const Vec3f& rTurnFront, f32 rate, bool isRailToEnd) {
    if (MR::isRailGoingToEnd(pStinkBug) == isRailToEnd) {
        MR::reverseRailDirection(pStinkBug);
    }
    MR::rotateVecDegree(&pStinkBug->mFront, rTurnFront, pStinkBug->mGravity,
                        MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f) * cNum_turnRailGround_f1 * -(isRailToEnd ? 1.0f : -1.0f));
}

void BossStinkBugFunction::turnRail(BossStinkBug* pStinkBug, const Vec3f& rTurnFront, f32 baseRoll, f32 rate, f32 speed, f32 heightOffset,
                                    f32 localHeightOffset, bool isRailToEnd) {
    f32 moveCoordParameter;

    // Slow to a stop over the first half, turn back on the rail, speed up over the second.
    if (rate <= cNum_turnRail_f1) {
        moveCoordParameter = speed * MR::getEaseInOutValue(1.0f - MR::normalize(rate, 0.0f, cNum_turnRail_f2), 0.0f, 1.0f, 1.0f);
    } else {
        if (MR::isRailGoingToEnd(pStinkBug) == isRailToEnd) {
            MR::reverseRailDirection(pStinkBug);
        }
        moveCoordParameter = speed * MR::getEaseInOutValue(MR::normalize(rate, cNum_turnRail_f3, 1.0f), 0.0f, 1.0f, 1.0f);
    }

    MR::moveCoord(pStinkBug, moveCoordParameter);
    Vec3f railPos(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPos, pStinkBug, heightOffset);

    f32 turnSign = (isRailToEnd ? 1.0f : -1.0f);
    f32 bend = cNum_turnRail_f4 * rate * (rate - 1.0f);
    f32 bendSq = bend * bend;
    f32 rollAngle = baseRoll * (1.0f - bendSq) + (cNum_turnRail_f5 * turnSign) * bendSq;
    pStinkBug->mRotation.z = MR::repeat(rollAngle, -cNum_turnRail_f6, cNum_turnRail_f7);

    MR::rotateVecDegree(&pStinkBug->mFront, rTurnFront, pStinkBug->mGravity, MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f) * cNum_turnRail_f8 * -turnSign);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    pStinkBug->mPosition.set(railPos);
}

void BossStinkBugFunction::regainToRail(BossStinkBug* pStinkBug, const Vec3f& rRailDiff, f32 rate, f32 speed, f32 heightOffset,
                                        f32 localHeightOffset) {
    pStinkBug->mRotation.z *= cNum_regainToRail_f1;
    MR::moveCoord(pStinkBug, speed);
    Vec3f railPos(MR::getRailPos(pStinkBug));
    addHeightOffset(&railPos, pStinkBug, heightOffset);
    addLocalHeightOffset(&railPos, pStinkBug, localHeightOffset);
    Vec3f dif = railPos - pStinkBug->mPosition;
    if (MR::isNearZero(dif)) {
        MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, dif, cNum_regainToRail_f2);
    } else {
        MR::turnDirectionDegree(pStinkBug, &pStinkBug->mFront, MR::getRailDirection(pStinkBug), cNum_regainToRail_f3);
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
    pStinkBug->throwBomb(cNum_throwBomb_f1, cNum_throwBomb_f2);
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
