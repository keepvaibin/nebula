#include "Game/Boss/PoltaStateGroundRockAttack.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/Boss/PoltaStateAttackGround.hpp"
#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace NrvPoltaStateGenerateBombTeresa {
    NEW_NERVE(PoltaStateGroundRockAttackNrvSign, PoltaStateGroundRockAttack, Sign);
    NEW_NERVE(PoltaStateGroundRockAttackNrvGenerate, PoltaStateGroundRockAttack, Generate);
    NEW_NERVE(PoltaStateGroundRockAttackNrvEnd, PoltaStateGroundRockAttack, End);
};  // namespace NrvPoltaStateGenerateBombTeresa

PoltaStateGroundRockAttack::PoltaStateGroundRockAttack(Polta* pPolta)
    : ActorStateBase< Polta >("[state]ポルタ地面岩攻撃", pPolta), mRockNum(4), mIsClockwise(false) {
    initNerve(GET_NERVE(PoltaStateGenerateBombTeresa, PoltaStateGroundRockAttackNrvSign));
}

void PoltaStateGroundRockAttack::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(PoltaStateGenerateBombTeresa, PoltaStateGroundRockAttackNrvSign));
}

void PoltaStateGroundRockAttack::exeSign() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::requestStartControllArm(getHost());
        PoltaFunction::startAction(getHost(), "GenerateGroundRockSign", true);
        MR::startSound(getHost(), "SE_BV_POLTA_PREP_GND_ROCK");
    }
    getHost()->rotateToPlayer();
    if (MR::isActionEnd(getHost())) {
        setNerve(GET_NERVE(PoltaStateGenerateBombTeresa, PoltaStateGroundRockAttackNrvGenerate));
    }
}

void PoltaStateGroundRockAttack::exeGenerate() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::startAction(getHost(), "GenerateGroundRockStart", true);
        MR::zeroVelocity(getHost());
        mIsClockwise = MR::isClockwiseToDir(getHost(), *MR::getPlayerLastMove(), *MR::getPlayerPos() - getHost()->mPosition);
    }

    if (MR::isBckOneTimeAndStopped(getHost())) {
        PoltaFunction::startAction(getHost(), "GenerateGroundRock", true);
        MR::startSound(getHost(), "SE_BV_POLTA_GEN_GND_ROCK");
        MR::startSound(getHost(), "SE_BM_POLTA_GEN_GND_ROCK");
    }
    if (MR::isLessEqualStep(this, 15 * (mRockNum - 1)) && MR::isIntervalStep(this, 15)) {
        // Rocks come in rings of four, each farther out; every new ring turns the other way.
        s32 rockIndex = getNerveStep() / 15;
        s32 ringStep = rockIndex % 4;

        if (rockIndex > 0 && !ringStep) {
            mIsClockwise = !mIsClockwise;
        }
        PoltaFunction::appearGroundRock(getHost(), (850.0f + (400.0f * ringStep)), mIsClockwise ? 20.0f : -20.0f);
    }
    if (MR::isGreaterStep(this, 15 * mRockNum + 90)) {
        setNerve(GET_NERVE(PoltaStateGenerateBombTeresa, PoltaStateGroundRockAttackNrvEnd));
    }
}

void PoltaStateGroundRockAttack::exeEnd() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::startAction(getHost(), "GenerateGroundRockEnd", true);
    }
    if (MR::isActionEnd(getHost())) {
        kill();
    }
}
