#include "Game/NPC/Peach.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvPeach {
    NEW_NERVE(PeachNrvWait, Peach, Wait);
};  // namespace NrvPeach

Peach::Peach(const char* pName) : NPCActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< Peach >);
}

void Peach::init(const TableIter& rIter) {
    NPCActorCaps caps("Peach");
    caps.setDefault();
    caps.mWaitNerve = GET_NERVE(Peach, PeachNrvWait);
    NPCActor::initialize(rIter, caps);
}

void Peach::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Help");
    }

    if (mMsgCtrl != nullptr) {
        MR::tryTalkNearPlayer(mMsgCtrl);
    }
}
