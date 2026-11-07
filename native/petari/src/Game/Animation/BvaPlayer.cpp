#include "Game/Animation/BvaPlayer.hpp"
#include "Game/Render/Model.hpp"

BvaPlayer::BvaPlayer(const ResTable* pResTable, Model* pModel) : AnmPlayerBase(pResTable), mModel(pModel) {
}

void BvaPlayer::calc() {
    Clip* pAnmVisibility = getAnmVisibility();
    if (pAnmVisibility == nullptr) {
        return;
    }

    reflectFrame();

    u32 shapeNum = mModel->getModelData()->getMeshCount();
    for (u32 idx = 0; idx < shapeNum; idx++) {
        u8 visibility = 0;
        pAnmVisibility->getVisibility(idx, &visibility);

        if (visibility != 0) {
            mModel->getMeshEntry(idx)->offFlag(MeshEntry::Flag_Hidden);
        } else {
            mModel->getMeshEntry(idx)->onFlag(MeshEntry::Flag_Hidden);
        }
    }
}

Clip* BvaPlayer::getAnmVisibility() {
    if (mAnmRes != nullptr) {
        return static_cast< Clip* >(mAnmRes);
    }

    return nullptr;
}
