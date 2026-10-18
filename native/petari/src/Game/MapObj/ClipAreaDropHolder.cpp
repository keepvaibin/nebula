#include "Game/MapObj/ClipAreaDropHolder.hpp"
#include "Game/MapObj/ClipAreaDrop.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

namespace {
    static const s32 sMaxClipAreaDrop = 32;
};  // namespace

ClipAreaDropHolder::ClipAreaDropHolder() : DeriveActorGroup< ClipAreaDrop >("\203\116\203\212\203\142\203\166\203\107\203\212\203\101\202\314\202\265\202\270\202\255\212\307\227\235" /* クリップエリアのしずく管理 */, ::sMaxClipAreaDrop) {
    ClipAreaDrop* pClipAreaDrop = nullptr;

    for (int i = 0; i < ::sMaxClipAreaDrop; i++) {
        pClipAreaDrop = new ClipAreaDrop("\203\116\203\212\203\142\203\166\203\107\203\212\203\101\202\314\202\265\202\270\202\255" /* クリップエリアのしずく */);
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

    bool appearClipAreaDrop(const TVec3f& rPos, f32 baseSize) {
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
