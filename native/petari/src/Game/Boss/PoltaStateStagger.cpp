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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 1800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 1800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f4, "exeWait.f4", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f5, "exeWait.f5", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f6, "exeWait.f6", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f7, "exeWait.f7", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f8, "exeWait.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f9, "exeWait.f9", 50.0f);

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

    if (distToStart >= cNum_exeWait_f1) {
        MR::addVelocity(getHost(), toStart * (distToStart - cNum_exeWait_f2));
        mIsClockwise = MR::isClockwiseToPlayer(getHost(), toStart);
    }

    if (MR::isNearPlayerHorizontal(getHost(), cNum_exeWait_f3)) {
        MR::addVelocityAwayFromTarget(getHost(), *MR::getPlayerPos(), cNum_exeWait_f4);
    }

    MR::addVelocityClockwiseToPlayer(getHost(), mIsClockwise ? cNum_exeWait_f5 : -cNum_exeWait_f6);
    MR::attenuateVelocity(getHost(), cNum_exeWait_f7);
    MR::addVelocityKeepHeight(getHost(), getHost()->mStartPos, cNum_exeWait_f8, cNum_exeWait_f9);
    getHost()->rotateToPlayer();
    if (MR::isIntervalStep(this, NebulaFrameClock::stepsOfMilliseconds(3500)) && PoltaFunction::appearBombTeresaFromRoot(getHost())) {
        MR::startSound(getHost(), "SE_BM_POLTA_GEN_BOMB_TERESA");
    }
}

bool PoltaStateStagger::isEnableSensor() const {
    return isNerve(GET_NERVE(PoltaStateStagger, PoltaStateStaggerNrvWait));
}
