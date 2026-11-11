#include "Game/MapObj/SpinDriverUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setShadowAndClipping_f1, "setShadowAndClipping.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setShadowAndClipping_f2, "setShadowAndClipping.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setShadowAndClipping_f3, "setShadowAndClipping.f3", 0.5f);

namespace {
    NEBULA_TUNABLE(f32, sSearchLength, 1500.0f);
};  // namespace

void SpinDriverUtil::setShadowAndClipping(LiveActor* pActor, Vec3f* pClippingCenter, f32 dropLength, f32 heightMargin, f32* pClippingRadius) {
    const Vec3f position(pActor->mPosition);
    const Vec3f gravity(pActor->mGravity);
    Vec3f dropPosition;
    dropPosition.set(position - gravity * heightMargin);

    // A negative length drops to the map, at most sSearchLength below.
    Vec3f dropEnd;
    f32 clippingRadius = heightMargin;
    if (dropLength < 0.0f) {
        if (MR::getFirstPolyOnLineToMap(&dropEnd, nullptr, dropPosition, gravity * (::sSearchLength + heightMargin))) {
            dropLength = dropEnd.distance(position);
            dropLength += heightMargin;
            clippingRadius = cNum_setShadowAndClipping_f1 * dropLength + heightMargin;
        } else {
            dropEnd.set(position);
            dropLength = 0.0f;
        }
    } else {
        dropEnd.set(position + gravity * dropLength);
        clippingRadius = cNum_setShadowAndClipping_f2 * dropLength + heightMargin;
    }

    pClippingCenter->set((dropEnd + position) * cNum_setShadowAndClipping_f3);
    MR::setClippingTypeSphere(pActor, clippingRadius, pClippingCenter);

    if (pClippingRadius != nullptr) {
        *pClippingRadius = clippingRadius;
    }

    MR::setShadowDropPosition(pActor, nullptr, dropPosition);
    MR::setShadowDropDirection(pActor, nullptr, gravity);
    MR::setShadowDropLength(pActor, nullptr, dropLength);
}
