#include "Game/NPC/ButlerExplain.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace NrvButlerExplain {
    NEW_NERVE(ButlerExplainNrvDemo, ButlerExplain, Demo);
};  // namespace NrvButlerExplain

ButlerExplain::ButlerExplain(const char* pName) : NPCActor(pName) {
}

void ButlerExplain::init(const JMapInfoIter& rIter) {
    NPCActorCaps caps("ButlerExplain");
    caps.setDefault();
    caps.mObjectName = "Butler";

    NPCActor::initialize(rIter, caps);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        DemoFunction::tryCreateDemoTalkAnimCtrlForActor(this, "DemoWithButler", nullptr);
        MR::registerDemoActionFunctor(this, MR::Functor(this, &ButlerExplain::startDemo), "\217\363\213\265\220\340\226\276[\212\112\216\156]" /* 状況説明[開始] */);
        DemoFunction::registerDemoTalkMessageCtrl(this, mMsgCtrl);
    }

    MR::tryRegisterDemoCast(this, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\120\213\101\212\322" /* グランドスター１帰還 */, rIter);
}

void ButlerExplain::control() {
    if (_D8) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_BUTLER_TRAMPLED");
    }

    NPCActor::control();
}

void ButlerExplain::startDemo() {
    setNerve(GET_NERVE(ButlerExplain, ButlerExplainNrvDemo));
}

void ButlerExplain::exeDemo() {
}
