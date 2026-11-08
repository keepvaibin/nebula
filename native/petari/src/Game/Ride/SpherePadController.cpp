#include "Game/Ride/SpherePadController.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>


#include "Game/System/NebulaInput.hpp"

SpherePadController::SpherePadController() : SphereController() {
}

f32 SpherePadController::calcMoveVector(Vec3f* pMoveDir, const Vec3f& rGravity) {
    return calcDirSphereMove(pMoveDir, rGravity, 0);
}

f32 SpherePadController::calcJumpPower() const {
    if (MR::testSystemTriggerA()) {
        return 1.0f;
    }

    return 0.0f;
}

void SpherePadController::update(const Vec3f&) {
}

void SpherePadController::calcXY(f32* pX, f32* pY) {
    *pX = MR::getMoveStickX(PLAYER_1);
    *pY = MR::getMoveStickY(PLAYER_1);
}

f32 SpherePadController::calcDirSphereMove(Vec3f* pMoveDir, const Vec3f& rBaseVec, u32 isFrontVec) {
    f32 x = 0.0f;
    f32 y = 0.0f;
    calcXY(&x, &y);

    f32 mag = x * x + y * y;
    mag = FloatUtil::sqrt(mag);

    if (mag >= 0.0001f) {
        Rot3f invMtx = MR::getCameraInvViewMtx();

        Vec3f dirX;
        Vec3f dirY;
        Vec3f dirZ;

        invMtx.getXDir(dirX);
        invMtx.getYDir(dirY);
        invMtx.getZDir(dirZ);

        MR::normalizeOrZero(&dirX);
        MR::normalizeOrZero(&dirY);
        MR::normalizeOrZero(&dirZ);

        if ((isFrontVec != 0 && dirZ.dot(rBaseVec) < 0.0f) || (isFrontVec == 0 && dirY.dot(-rBaseVec) < 0.0f)) {
            dirX = -dirX;
            dirY = -dirY;
            dirZ = -dirZ;
        }

        Rot3f rotMtx;
        if (isFrontVec != 0) {
            rotMtx.setRotate(dirZ, rBaseVec);
            rotMtx.mult33(dirX);
            rotMtx.mult33(dirY);
            pMoveDir->set(-dirX * x - dirY * y);
        } else {
            rotMtx.setRotate(dirY, -rBaseVec);
            rotMtx.mult33(dirX);
            rotMtx.mult33(dirZ);
            pMoveDir->set(dirX * x - dirZ * y);
        }

        MR::separateScalarAndDirection(&mag, pMoveDir, *pMoveDir);
    } else {
        pMoveDir->zero();
    }

    if (mag > 1.0f) {
        mag = 1.0f;
    }

    return mag;
}
