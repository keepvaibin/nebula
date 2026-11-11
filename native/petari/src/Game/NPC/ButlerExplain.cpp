#include "Game/NPC/ButlerExplain.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvButlerExplain {
    NEW_NERVE(ButlerExplainNrvDemo, ButlerExplain, Demo);
};  // namespace NrvButlerExplain

ButlerExplain::ButlerExplain(const char* pName) : NPCActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ButlerExplain >);
}

void ButlerExplain::init(const TableIter& rIter) {
    NPCActorCaps caps("ButlerExplain");
    caps.setDefault();
    caps.mObjectName = "Butler";

    NPCActor::initialize(rIter, caps);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        DemoFunction::tryCreateDemoTalkAnimCtrlForActor(this, "DemoWithButler", nullptr);
        MR::registerDemoActionFunctor(this, MR::Functor(this, &ButlerExplain::startDemo), "状況説明[開始]");
        DemoFunction::registerDemoTalkMessageCtrl(this, mMsgCtrl);
    }

    MR::tryRegisterDemoCast(this, "グランドスター１帰還", rIter);
}

void ButlerExplain::control() {
    if (mIsTrampledStart) {
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
