#include "Game/MapObj/ExterminationChecker.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/SkeletalFishBaby.hpp"
#include "Game/Enemy/Kuribo.hpp"
#include "Game/Enemy/Meramera.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/KeySwitch.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeTryStartDemoAppear_1, "exeTryStartDemoAppear.1", 5);

namespace NrvExterminationChecker {
    NEW_NERVE(ExterminationCheckerNrvWatching, ExterminationChecker, Watching);
    NEW_NERVE(ExterminationCheckerNrvTryStartDemoAppear, ExterminationChecker, TryStartDemoAppear);
    NEW_NERVE(ExterminationCheckerNrvAppearStar, ExterminationChecker, AppearStar);
    NEW_NERVE(ExterminationCheckerNrvAppearKeySwitch, ExterminationChecker, AppearKeySwitch);
};  // namespace NrvExterminationChecker

namespace {
    template < typename T >
    LiveActor* createLiveActor(const char* pName) {
        return new T(pName);
    }

    static const ExterminationEntry sCreateTable[] = {{"ChildKuribo", MR::createNoItemKuriboActor},
                                                      {"ChildSkeletalFishBaby", createLiveActor< SkeletalFishBaby >},
                                                      {"ChildMeramera", createLiveActor< Meramera >}};

    CreationFunc findEntry(const char* pName) {
        for (s32 j = 0; j < ARRAY_SIZE(::sCreateTable); j++) {
            if (::sCreateTable[j].mChildName != nullptr && MR::isEqualStringCase(::sCreateTable[j].mChildName, pName)) {
                return ::sCreateTable[j].mCreationFunc;
            }
        }

        return nullptr;
    }
};  // namespace

ExterminationChecker::ExterminationChecker(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ExterminationChecker >);
    mGroup = nullptr;
    mKeySwitch = nullptr;
    mKeySwitchPos.x = 0.0f;
    mKeySwitchPos.y = 0.0f;
    mKeySwitchPos.z = 0.0f;
    mIsPowerStar = 1;
    mIsKeySwitch = 0;
}

void ExterminationChecker::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    s32 objNum = MR::getChildObjNum(rIter);
    mGroup = new LiveActorGroup("アクター管理", objNum);

    for (s32 i = 0; i < objNum; i++) {
        const char* objName = nullptr;
        MR::getChildObjName(&objName, rIter, i);
        // A child of a kind the table lacks (a mod's row) is not made.
        CreationFunc pCreate = ::findEntry(objName);
        if (pCreate == nullptr) {
            continue;
        }
        LiveActor* actor = pCreate(MR::getJapaneseObjectName(objName));
        MR::initChildObj(actor, rIter, i);
        mGroup->registerActor(actor);
    }

    MR::invalidateClipping(this);
    MR::useStageSwitchSleep(this, rIter);

    if (mIsKeySwitch) {
        mKeySwitch = new KeySwitch("全滅用キースイッチ");
        mKeySwitch->initKeySwitchByOwner(rIter);
    }

    if (mIsPowerStar) {
        MR::declarePowerStar(this);
    }

    initNerve(GET_NERVE(ExterminationChecker, ExterminationCheckerNrvWatching));

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void ExterminationChecker::control() {
}

void ExterminationChecker::exeWatching() {
    for (s32 i = 0; i < mGroup->getObjNum(); i++) {
        if (MR::isDead(mGroup->getActor(i))) {
            continue;
        }

        mKeySwitchPos.set(mGroup->getActor(i)->mPosition);
        return;
    }

    if (mIsPowerStar) {
        setNerve(GET_NERVE(ExterminationChecker, ExterminationCheckerNrvTryStartDemoAppear));
    }

    if (mIsKeySwitch) {
        setNerve(GET_NERVE(ExterminationChecker, ExterminationCheckerNrvAppearKeySwitch));
    }
}

void ExterminationChecker::exeTryStartDemoAppear() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(10);
    }

    if (MR::isGreaterStep(this, cStep_exeTryStartDemoAppear_1)) {
        if (MR::tryStartDemo(this, "全滅スター出現")) {
            setNerve(GET_NERVE(ExterminationChecker, ExterminationCheckerNrvAppearStar));
        }
    }
}

void ExterminationChecker::exeAppearStar() {
    if (MR::isStep(this, 0)) {
        MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::endDemo(this, "全滅スター出現");
        MR::startAfterBossBGM();
        MR::requestAppearPowerStar(this);
        kill();
    }
}

void ExterminationChecker::exeAppearKeySwitch() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        mKeySwitch->appearKeySwitch(mKeySwitchPos);
        kill();
    }
}

namespace MR {
    NameObj* createExterminationPowerStar(const char* pName) {
        ExterminationChecker* checker = new ExterminationChecker(pName);
        checker->mIsPowerStar = 1;
        checker->mIsKeySwitch = 0;
        return checker;
    }

    NameObj* createExterminationKeySwitch(const char* pName) {
        ExterminationChecker* checker = new ExterminationChecker(pName);
        checker->mIsPowerStar = 0;
        checker->mIsKeySwitch = 1;
        return checker;
    }
};  // namespace MR
