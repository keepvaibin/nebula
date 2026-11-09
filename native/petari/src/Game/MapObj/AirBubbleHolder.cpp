#include "Game/MapObj/AirBubbleHolder.hpp"
#include "Game/MapObj/AirBubble.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    const s32 sCreateAirBubbleNum = 32;
};  // namespace

AirBubbleHolder::AirBubbleHolder(const char* pName) : DeriveActorGroup< AirBubble >(pName, ::sCreateAirBubbleNum) {
}

void AirBubbleHolder::appearAirBubble(const Vec3f& rTrans, s32 lifeTime) {
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

    void appearAirBubble(const Vec3f& rTrans, s32 lifeTime) {
        MR::getSceneObj< AirBubbleHolder >(SceneObj_AirBubbleHolder)->appearAirBubble(rTrans, lifeTime);
    }
};  // namespace MR

void AirBubbleHolder::init(const TableIter& /*rIter*/) {
    createAirBubbles();
}

void AirBubbleHolder::createAirBubbles() {
    NebulaArena::CurrentScope scope(Arenas::scene());
    for (s32 i = 0; i < ::sCreateAirBubbleNum; i++) {
        AirBubble* pAirBubble = new AirBubble("空気アワ(共用)");
        pAirBubble->initWithoutIter();
        pAirBubble->makeActorDead();
        registerActor(pAirBubble);
    }
}
