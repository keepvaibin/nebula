#include "Game/Enemy/SamboFunction.hpp"
#include "Game/Enemy/AnimScaleController.hpp"

AnimScaleController* SamboFunction::createAnimScaleController(AnimScaleParam* pParam) {
    pParam->mPointerTime = 4.0f;
    pParam->mPointerBase = 1.0f;
    pParam->mPointerAmplitude = 0.06f;
    pParam->mPointerFreq = 2.0f;
    pParam->mHitReactionVelocity = 0.06f;
    pParam->mHitReactionSpring = 0.4f;
    pParam->mHitReactionDamping = 0.8f;
    return new AnimScaleController(pParam);
}
