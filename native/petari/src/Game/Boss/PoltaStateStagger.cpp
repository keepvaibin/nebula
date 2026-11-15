#include "Game/Boss/PoltaStateStagger.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaActionBase.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>

namespace NrvPoltaStateStagger {
    NEW_NERVE(PoltaStateStaggerNrvWait, PoltaStateStagger, Wait);
};  // namespace NrvPoltaStateStagger

PoltaStateStagger::PoltaStateStagger(Polta* pPolta)
    : ActorStateBase< Polta >("[state]ポルタ弱り状態", pPolta), mActionName("Stagger"), mIsClockwise(true) {
    initNerve(GET_NERVE(PoltaStateStagger, PoltaStateStaggerNrvWait));
}

void PoltaStateStagger::setActionName(const char* pActionName) {
    mActionName = pActionName;
}

void PoltaStateStagger::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(PoltaStateStagger, PoltaStateStaggerNrvWait));
}

void PoltaStateStagger::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(getHost());
        PoltaFunction::startBckBody(getHost(), mActionName);
    }
    Vec3f toStart;
    f32 distToStart;
    MR::separateScalarAndDirection(&distToStart, &toStart, getHost()->mStartPos - getHost()->mPosition);

    if (distToStart >= 1800.0f) {
        MR::addVelocity(getHost(), toStart * (distToStart - 1800.0f));
        mIsClockwise = MR::isClockwiseToPlayer(getHost(), toStart);
    }

    if (MR::isNearPlayerHorizontal(getHost(), 1500.0f)) {
        MR::addVelocityAwayFromTarget(getHost(), *MR::getPlayerPos(), 0.7f);
    }

    MR::addVelocityClockwiseToPlayer(getHost(), mIsClockwise ? 0.8f : -0.8f);
    MR::attenuateVelocity(getHost(), 0.9f);
    MR::addVelocityKeepHeight(getHost(), getHost()->mStartPos, 0.5f, 50.0f);
    getHost()->rotateToPlayer();
    if (MR::isIntervalStep(this, NebulaFrameClock::stepsOfMilliseconds(3500)) && PoltaFunction::appearBombTeresaFromRoot(getHost())) {
        MR::startSound(getHost(), "SE_BM_POLTA_GEN_BOMB_TERESA");
    }
}

bool PoltaStateStagger::isEnableSensor() const {
    return isNerve(GET_NERVE(PoltaStateStagger, PoltaStateStaggerNrvWait));
}
