#include "Game/Ride/SphereController.hpp"

SphereController::SphereController() : _4(0) {
}

f32 SphereController::calcMoveVector(Vec3f* pDst, const Vec3f& rSrc) {
    return pDst->x = pDst->y = pDst->z = 0.0f;
}

void SphereController::update(const Vec3f& sSrc) {
}

bool SphereController::doBrake() const {
    return false;
}

f32 SphereController::calcJumpPower() const {
    return 0.0f;
}
