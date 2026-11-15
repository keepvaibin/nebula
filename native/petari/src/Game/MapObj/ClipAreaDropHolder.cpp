#include "Game/MapObj/ClipAreaDropHolder.hpp"
#include "Game/MapObj/ClipAreaDrop.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

namespace {
    const s32 sMaxClipAreaDrop = 32;
};  // namespace

ClipAreaDropHolder::ClipAreaDropHolder() : DeriveActorGroup< ClipAreaDrop >("クリップエリアのしずく管理", ::sMaxClipAreaDrop) {
    for (s32 i = 0; i < ::sMaxClipAreaDrop; i++) {
        ClipAreaDrop* pClipAreaDrop = new ClipAreaDrop("クリップエリアのしずく");
        pClipAreaDrop->initWithoutIter();
        registerActor(pClipAreaDrop);
    }
}

namespace MR {
    NameObj* createClipAreaDropHolder() {
        return MR::createSceneObj(SceneObj_ClipAreaDropHolder);
    }

    ClipAreaDrop* getDeadClipAreaDrop() {
        return MR::getSceneObj< ClipAreaDropHolder >(SceneObj_ClipAreaDropHolder)->getDeadMember();
    }

    bool appearClipAreaDrop(const Vec3f& rPos, f32 baseSize) {
        ClipAreaDrop* pClipAreaDrop = getDeadClipAreaDrop();

        if (pClipAreaDrop == nullptr) {
            return false;
        }

        pClipAreaDrop->mPosition.set(rPos);
        pClipAreaDrop->setBaseSize(baseSize);
        pClipAreaDrop->appear();

        return true;
    }
};  // namespace MR
