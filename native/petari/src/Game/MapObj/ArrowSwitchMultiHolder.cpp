#include "Game/MapObj/ArrowSwitchMultiHolder.hpp"
#include "Game/MapObj/ArrowSwitchMulti.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/JMapIdInfo.hpp"

namespace {
    static const s32 sMaxArrowSwitchMulti = 16;
};  // namespace

namespace {
    ArrowSwitchMultiHolder* getArrowSwitchMultiHolder() {
        return MR::getSceneObj< ArrowSwitchMultiHolder >(SceneObj_ArrowSwitchMultiHolder);
    }
};  // namespace

ArrowSwitchMultiHolder::ArrowSwitchMultiHolder() : DeriveActorGroup("\225\241\220\224\225\373\214\374\226\356\210\363\203\130\203\103\203\142\203\140\212\307\227\235" /* 複数方向矢印スイッチ管理 */, ::sMaxArrowSwitchMulti) {
}

ArrowSwitchMulti* ArrowSwitchMultiHolder::findSwitch(const JMapIdInfo* pIdInfo) {
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
        ::getArrowSwitchMultiHolder()->findSwitch(pTarget->mIdInfo)->registerTarget(pTarget);
    }
};  // namespace MR
