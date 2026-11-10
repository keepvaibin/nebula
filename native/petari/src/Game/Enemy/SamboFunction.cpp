#include "Game/Enemy/SamboFunction.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f1, "createAnimScaleController.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f2, "createAnimScaleController.f2", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f3, "createAnimScaleController.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f4, "createAnimScaleController.f4", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f5, "createAnimScaleController.f5", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_createAnimScaleController_f6, "createAnimScaleController.f6", 0.8f);

AnimScaleController* SamboFunction::createAnimScaleController(AnimScaleParam* pParam) {
    pParam->mPointerTime = cNum_createAnimScaleController_f1;
    pParam->mPointerBase = 1.0f;
    pParam->mPointerAmplitude = cNum_createAnimScaleController_f2;
    pParam->mPointerFreq = cNum_createAnimScaleController_f3;
    pParam->mHitReactionVelocity = cNum_createAnimScaleController_f4;
    pParam->mHitReactionSpring = cNum_createAnimScaleController_f5;
    pParam->mHitReactionDamping = cNum_createAnimScaleController_f6;
    return new AnimScaleController(pParam);
}
