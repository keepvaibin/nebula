#include "Game/Enemy/UnizoLauncher.hpp"
#include "Game/Enemy/Unizo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace {
    const s32 sUnizoNumber = 4;
    NEBULA_TUNABLE(s32, sUnizoInter, 300);
};  // namespace

namespace NrvUnizoLauncher {
    NEW_NERVE(UnizoLauncherNrvWait, UnizoLauncher, Wait);
    NEW_NERVE(UnizoLauncherNrvLaunch, UnizoLauncher, Launch);
};  // namespace NrvUnizoLauncher

UnizoLauncher::UnizoLauncher(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< UnizoLauncher >);
}

void UnizoLauncher::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemyMovement(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "Body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    initSound(8, false);
    initNerve(GET_NERVE(UnizoLauncher, UnizoLauncherNrvWait));

    mUnizoNum = ::sUnizoNumber;
    mUnizos = new Unizo*[::sUnizoNumber];
    MR::setGroupClipping(this, rIter, 16);

    for (s32 i = 0; i < mUnizoNum; i++) {
        mUnizos[i] = new Unizo("陸ウニゾー");
        mUnizos[i]->init(rIter);
        mUnizos[i]->makeActorDead();
        MR::setGroupClipping(mUnizos[i], rIter, 16);
    }

    makeActorAppeared();
}

void UnizoLauncher::exeLaunch() {
    if (MR::isFirstStep(this)) {
        for (s32 i = 0; i < mUnizoNum; i++) {
            Unizo* const pUnizo = mUnizos[i];
            if (MR::isDead(pUnizo)) {
                MR::copyTransRotateScale(this, pUnizo);
                pUnizo->mPosition.z += i;
                pUnizo->appear();
                break;
            }
        }
    }

    setNerve(GET_NERVE(UnizoLauncher, UnizoLauncherNrvWait));
}

void UnizoLauncher::exeWait() {
    if (MR::isGreaterStep(this, ::sUnizoInter)) {
        setNerve(GET_NERVE(UnizoLauncher, UnizoLauncherNrvLaunch));
    }
}
