#include "Game/Ride/SledRopePoint.hpp"
#include "Game/LiveActor/Binder.hpp"

SledRopePoint::SledRopePoint(const Vec3f& rPos, f32 bindRadius)
    : SwingRopePoint(rPos), mBinder(new Binder(nullptr, &mPosition, new Vec3f(0.0f, -1.0f, 0.0f), bindRadius, 0.0f, 8)) {
}

void SledRopePoint::updatePos(f32 friction) {
    mPosition.add(mBinder->bind(mVelocity));
    mVelocity.scale(friction);
}

void SledRopePoint::updatePosAndAxis(const Vec3f& rAxis, f32 friction) {
    updatePos(friction);
    updateAxis(rAxis);
}

void SledRopePoint::bindToMapCollision(f32 rebound) {
    for (u32 plane = 0; plane < mBinder->mPlaneNum; plane++) {
        const Vec3f* normal = mBinder->getPlane(plane)->mParentTriangle.getNormal(0);
        f32 dot = mVelocity.dot(*normal);
        if (dot < 0.0f) {
            Vec3f reboundVec = *normal;
            reboundVec.scale(-(1.0f + rebound) * dot);
            mVelocity.add(reboundVec);
        }
    }
}
