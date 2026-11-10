#include "Game/Boss/DinoPackunStateAwake.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeFind_f1, "exeFind.f1", 0.015f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFind_f2, "exeFind.f2", 4.0f);

namespace NrvDinoPackunStateAwake {
    NEW_NERVE(DinoPackunStateAwakeNrvFind, DinoPackunStateAwake, Find);
};  // namespace NrvDinoPackunStateAwake

DinoPackunStateAwake::DinoPackunStateAwake(DinoPackun* pHost) : ActorStateBase< DinoPackun >("ディノパックン気付く", pHost) {
    initNerve(GET_NERVE(DinoPackunStateAwake, DinoPackunStateAwakeNrvFind));
}

void DinoPackunStateAwake::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(DinoPackunStateAwake, DinoPackunStateAwakeNrvFind));
}

void DinoPackunStateAwake::kill() {
    mIsDead = true;
    getHost()->mTail->mHostVelocityRate = 0.0f;
}

void DinoPackunStateAwake::exeFind() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "Find");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_FIND");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    f32 rate = MR::calcNerveEaseOutRate(this, NebulaFrameClock::stepsOfMilliseconds(500));
    getHost()->mTail->mHostVelocityRate = cNum_exeFind_f1 * rate;
    MR::turnDirectionToPlayerDegree(getHost(), &getHost()->mFront, cNum_exeFind_f2);
    getHost()->updateNormalVelocity();

    if (MR::isBckStopped(getHost())) {
        kill();
    }
}
