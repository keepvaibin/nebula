#include "Game/MapObj/AirBubbleHolder.hpp"
#include "Game/MapObj/AirBubble.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"

namespace {
    static const s32 sCreateAirBubbleNum = 32;
};  // namespace

AirBubbleHolder::AirBubbleHolder(const char* pName) : DeriveActorGroup< AirBubble >(pName, ::sCreateAirBubbleNum) {
}

void AirBubbleHolder::appearAirBubble(const TVec3f& rTrans, s32 lifeTime) {
    AirBubble* pAirBubble = getDeadMember();
    if (pAirBubble == nullptr) {
        const s32 first = getObjNum();
        createAirBubbles();
        for (s32 i = first; i < getObjNum(); i++) {
            getMember(i)->initAfterPlacement();
        }
        pAirBubble = getDeadMember();
    }

    pAirBubble->appearMove(rTrans, lifeTime);
}

namespace MR {
    AirBubbleHolder* createAirBubbleHolder() {
        return static_cast< AirBubbleHolder* >(MR::createSceneObj(SceneObj_AirBubbleHolder));
    }

    void appearAirBubble(const TVec3f& rTrans, s32 lifeTime) {
        MR::getSceneObj< AirBubbleHolder >(SceneObj_AirBubbleHolder)->appearAirBubble(rTrans, lifeTime);
    }
};  // namespace MR

AirBubbleHolder::~AirBubbleHolder() {
}

void AirBubbleHolder::init(const JMapInfoIter& rIter) {
    createAirBubbles();
}

void AirBubbleHolder::createAirBubbles() {
    MR::CurrentHeapRestorer heapRestorer(MR::getSceneHeap());
    for (s32 i = 0; i < ::sCreateAirBubbleNum; i++) {
        AirBubble* pAirBubble = new AirBubble("\213\363\213\103\203\101\203\217(\213\244\227\160)" /* 空気アワ(共用) */);
        pAirBubble->initWithoutIter();
        pAirBubble->makeActorDead();
        registerActor(pAirBubble);
    }
}
