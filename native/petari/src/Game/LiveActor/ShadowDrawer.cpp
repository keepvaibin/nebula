#include "Game/LiveActor/ShadowDrawer.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // A step in which a shape's anchor moves farther than this is a jump (a ledge, a warp): nothing blends across it.
    // Below the host's teleport test for a model (200 units and half the eye distance), so the shadow gives up first.
    NEBULA_TUNABLE(f32, cAnchorJumpDistance, 300.0f);
};  // namespace

ShadowDrawer::ShadowDrawer(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< ShadowDrawer >);
    mController = 0;
    mDrawAnchor.zero();
    mAnchorJumps = 0;
    mHasAnchor = false;
}

void ShadowDrawer::calcDrawAnchor(Vec3f* pOut) const {
    if (mController != nullptr) {
        mController->getDropPos(pOut);
    } else {
        pOut->zero();
    }
}

u32 ShadowDrawer::calcDrawLives() const {
    return mController != nullptr ? mController->getHost()->mMotionEpoch : 0u;
}

void ShadowDrawer::beginDrawObject() const {
    Vec3f anchor;
    calcDrawAnchor(&anchor);
    if (mHasAnchor && anchor.squared(mDrawAnchor) > ::cAnchorJumpDistance * ::cAnchorJumpDistance) {
        mAnchorJumps++;
    }

    mDrawAnchor = anchor;
    mHasAnchor = true;
    // The slot the category list gave this drawer stays; the generation names the life and the run of smooth steps.
    Frame::setObject(Frame::objectSlot(), calcDrawLives() * 4099u + mAnchorJumps);
}

ShadowController* ShadowDrawer::getController() const {
    return mController;
}

void ShadowDrawer::setShadowController(ShadowController* pController) {
    mController = pController;
}
