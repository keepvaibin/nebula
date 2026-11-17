#include "Game/Player/MarioHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

MarioHolder::MarioHolder() : NameObj("マリオ保持") {
    lumacore::setClass(this, lumacore::kClassId< MarioHolder >);
    mActor = nullptr;
}

MarioHolder::~MarioHolder() {
}

void MarioHolder::setMarioActor(MarioActor* pActor) {
    mActor = pActor;
}

MarioActor* MarioHolder::getMarioActor() const {
    return mActor;
}

namespace MR {
    // Null while no scene holds a player (the boot, a scene still being built): a mod may ask then
    // (docs/modding/reference/lumascript/detailed-rules.md); the game itself asks only with one.
    MarioHolder* getMarioHolder() {
        return MR::isExistSceneObj(SceneObj_MarioHolder) ? MR::getSceneObj< MarioHolder >(SceneObj_MarioHolder) : nullptr;
    }
};  // namespace MR
