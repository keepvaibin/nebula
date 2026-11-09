#include "Game/Boss/PoltaStateGenerateRock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const s32 cPatternLength = 7;
    // 1 marks a black (bomb boo) rock.
    const s32 cRockPattern[][cPatternLength] = {
        {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 0, 0, 1}, {0, 0, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0},
        {1, 0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 1, 0}, {0, 0, 0, 1, 0, 0, 1}, {0, 0, 0, 0, 1, 0, 0},
    };
    // The draw is a float in [0, 9) cut to an int, so the last pattern is never picked.
    const s32 cPatternDrawMax = 9;
    const s32 cRockInterval = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 cRockCircleRadius = 600.0f;
    const f32 cYellowRockRate = 0.08f;
}  // namespace

namespace NrvPoltaStateGenerateRock {
    NEW_NERVE(PoltaStateGenerateRockNrvSign, PoltaStateGenerateRock, Sign);
    NEW_NERVE(PoltaStateGenerateRockNrvGenerate, PoltaStateGenerateRock, Generate);
    NEW_NERVE(PoltaStateGenerateRockNrvEnd, PoltaStateGenerateRock, End);
}  // namespace NrvPoltaStateGenerateRock

PoltaStateGenerateRock::PoltaStateGenerateRock(Polta* pPolta)
    : ActorStateBase< Polta >("ポルタ岩生成", pPolta), mPatternIndex(), mRockCount(), mRockNum(5), mEndDelayStep(180) {
    initNerve(GET_NERVE(PoltaStateGenerateRock, PoltaStateGenerateRockNrvSign));
}

void PoltaStateGenerateRock::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(PoltaStateGenerateRock, PoltaStateGenerateRockNrvSign));
    mRockCount = 0;
}

void PoltaStateGenerateRock::exeSign() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::requestStartControllArm(getHost());
        PoltaFunction::startAction(getHost(), "GenerateRockStart", true);
        MR::startSound(getHost(), "SE_BV_POLTA_GEN_ROCK");
    }

    getHost()->rotateToPlayer();
    if (MR::isActionEnd(getHost())) {
        NerveExecutor::setNerve(GET_NERVE(PoltaStateGenerateRock, PoltaStateGenerateRockNrvGenerate));
    }
}

// A marked slot gives a black rock while bomb boos are left; otherwise a rock may turn yellow while coins are left.
void PoltaStateGenerateRock::exeGenerate() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::startAction(getHost(), "GenerateRock", true);
        MR::zeroVelocity(getHost());
        mPatternIndex = MR::getRandom(static_cast< s32 >(0), ::cPatternDrawMax);
    }

    getHost()->rotateToPlayer();
    if (MR::isIntervalStep(this, ::cRockInterval)) {
        const s32 slot = mRockCount % ::cPatternLength;

        PoltaRock::Color color = PoltaRock::Color_White;
        if (::cRockPattern[mPatternIndex][slot] && !PoltaFunction::isMaxGenerateBombTeresa(getHost())) {
            color = PoltaRock::Color_Black;
        } else if (MR::getRandom(0.0f, 1.0f) < ::cYellowRockRate && MR::getDeclareRemnantCoinCount(getHost()) > 0) {
            color = PoltaRock::Color_Yellow;
        }

        PoltaFunction::appearRockCircle(getHost(), getHost()->mPosition, ::cRockCircleRadius, slot, color);
        mRockCount++;
    }

    if (mRockCount >= mRockNum) {
        setNerve(GET_NERVE(PoltaStateGenerateRock, PoltaStateGenerateRockNrvEnd));
    }
}

void PoltaStateGenerateRock::exeEnd() {
    if (MR::isStep(this, mEndDelayStep)) {
        PoltaFunction::startAction(getHost(), "GenerateRockToWait", true);
    }

    getHost()->rotateToPlayer();
    if (MR::isGreaterStep(this, mEndDelayStep) && MR::isActionEnd(getHost())) {
        kill();
    }
}
