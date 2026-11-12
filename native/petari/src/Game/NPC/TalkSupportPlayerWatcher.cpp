#include "Game/NPC/TalkSupportPlayerWatcher.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

namespace {
    const f32 sPlayerSpeedMin = 4.0f;
};  // namespace

TalkSupportPlayerWatcher::TalkSupportPlayerWatcher() {
}

void TalkSupportPlayerWatcher::update() {
}

bool TalkSupportPlayerWatcher::isEnableTalkPlayerStateNormal() const {
    if (MR::isPlayerInWaterMode()) {
        return false;
    }

    if (MR::isOffPlayerControl()) {
        return false;
    }

    if (!MR::isNearZero(MR::getMoveStickX(PLAYER_1)) || !MR::isNearZero(MR::getMoveStickY(PLAYER_1))) {
        return false;
    }

    return isEnableTalkPlayerStateEvent();
}

bool TalkSupportPlayerWatcher::isEnableTalkPlayerStateEvent() const {
    if (MR::isPlayerInBind()) {
        return false;
    }

    if (MR::isPlayerRefuseTalk()) {
        return false;
    }

    if (MR::isPlayerElementModeTeresa()) {
        return true;
    }

    if (MR::isPlayerInWaterMode() || MR::isInWater(MR::getPlayerDemoActor(), Vec3f(0.0f, 0.0f, 0.0f))) {
        return true;
    }

    return MR::isOnGroundPlayer();
}

bool TalkSupportPlayerWatcher::isEnableTalkPlayerSpeed() const {
    return MR::getPlayerVelocity()->length() < ::sPlayerSpeedMin;
}
