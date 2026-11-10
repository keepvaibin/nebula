#include "Game/MapObj/ArrowSwitchMultiHolder.hpp"
#include "Game/MapObj/ArrowSwitchMulti.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, sMaxArrowSwitchMulti, 16);

    ArrowSwitchMultiHolder* getArrowSwitchMultiHolder() {
        return MR::getSceneObj< ArrowSwitchMultiHolder >(SceneObj_ArrowSwitchMultiHolder);
    }
};  // namespace

ArrowSwitchMultiHolder::ArrowSwitchMultiHolder() : DeriveActorGroup("複数方向矢印スイッチ管理", ::sMaxArrowSwitchMulti) {
    lumacore::setClass(this, lumacore::kClassId< ArrowSwitchMultiHolder >);
}

ArrowSwitchMulti* ArrowSwitchMultiHolder::findSwitch(const PlacementId* pIdInfo) {
    for (s32 i = 0; i < getObjNum(); i++) {
        ArrowSwitchMulti* pSwitch = getMember(i);

        if (*pSwitch->mIdInfo == *pIdInfo) {
            return pSwitch;
        }
    }

    return nullptr;
}

namespace MR {
    void createArrowSwitchMultiHolder() {
        MR::createSceneObj(SceneObj_ArrowSwitchMultiHolder);
    }

    void registerArrowSwitchMulti(ArrowSwitchMulti* pSwitch) {
        ::getArrowSwitchMultiHolder()->registerActor(pSwitch);
    }

    void registerArrowSwitchTarget(ArrowSwitchTarget* pTarget) {
        // A target whose id no switch has (a mod's data) is not registered.
        ArrowSwitchMulti* const pSwitch = ::getArrowSwitchMultiHolder()->findSwitch(pTarget->mIdInfo);
        if (pSwitch != nullptr) {
            pSwitch->registerTarget(pTarget);
        }
    }
};  // namespace MR
