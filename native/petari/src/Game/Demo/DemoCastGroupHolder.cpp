#include "Game/Demo/DemoCastGroupHolder.hpp"
#include "Game/Demo/DemoCastGroup.hpp"
#include "Game/Util/ObjUtil.hpp"

DemoCastGroupHolder::DemoCastGroupHolder() : NameObjGroup("\203\146\203\202\212\326\214\127\216\322\203\117\203\213\201\133\203\166\225\333\216\235" /* デモ関係者グループ保持 */, 32) {
}

bool DemoCastGroupHolder::tryRegisterDemoActor(LiveActor* pActor, const JMapInfoIter& rIter, const JMapIdInfo& rInfo) const {
    for (s32 i = 0; i < getObjNum(); i++) {
        if (getCastGroup(i)->tryRegisterDemoActor(pActor, rIter, rInfo)) {
            return true;
        }
    }

    return false;
}

bool DemoCastGroupHolder::tryRegisterDemoActor(LiveActor* pActor, const char* pName, const JMapInfoIter& rIter) const {
    for (s32 i = 0; i < getObjNum(); i++) {
        if (getCastGroup(i)->tryRegisterDemoActor(pActor, pName, rIter)) {
            return true;
        }
    }

    return false;
}

DemoCastGroup* DemoCastGroupHolder::getCastGroup(int index) const {
    return static_cast< DemoCastGroup* >(getObj(index));
}

DemoCastGroup* DemoCastGroupHolder::findCastGroup(const char* pName) const {
    for (s32 i = 0; i < getObjNum(); i++) {
        DemoCastGroup* castGroup = getCastGroup(i);

        if (MR::isName(castGroup, pName)) {
            return castGroup;
        }
    }

    return nullptr;
}
