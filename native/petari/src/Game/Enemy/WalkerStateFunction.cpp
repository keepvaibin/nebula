#include "Game/Enemy/WalkerStateFunction.hpp"
#include "Game/Enemy/WalkerStateParam.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Tunable.hpp"

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_1, "calcPassiveMovement.reboundVelocityFromEachCollision.1", -1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_2, "calcPassiveMovement.reboundVelocityFromEachCollision.2", -1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_3, "calcPassiveMovement.reboundVelocityFromEachCollision.3", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_4, "calcPassiveMovement.reboundVelocityFromEachCollision.4", 0.0f);

bool WalkerStateFunction::isInSightPlayer(const LiveActor* pActor, const Vec3f& rDirection, const WalkerStateParam* pStateParam) {
    return MR::isInSightFanPlayer(pActor, rDirection, pStateParam->mPlayerNearDistance, pStateParam->mPlayerSightFanDegreeH,
                                  pStateParam->mPlayerSightFanDegreeV);
}

void WalkerStateFunction::calcPassiveMovement(LiveActor* pActor, const WalkerStateParam* pStateParam) {
    MR::reboundVelocityFromEachCollision(pActor, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_1, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_2, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_3, cNum_calcPassiveMovement_reboundVelocityFromEachCollision_4);
    if (!MR::isOnGround(pActor)) {
        MR::addVelocityToGravity(pActor, pStateParam->mGravityAccel);
        MR::attenuateVelocity(pActor, pStateParam->mAirFriction);
    } else {
        MR::attenuateVelocity(pActor, pStateParam->mGroundFriction);
    }
}
