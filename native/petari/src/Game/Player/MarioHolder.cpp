#include "Game/Player/MarioHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

MarioHolder::MarioHolder() : NameObj("\203\175\203\212\203\111\225\333\216\235" /* マリオ保持 */) {
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
    MarioHolder* getMarioHolder() {
        return MR::getSceneObj< MarioHolder >(SceneObj_MarioHolder);
    }
};  // namespace MR
