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

    u32 shapeNum = mModel->getModelData()->getShapeNum();
    for (u32 idx = 0; idx < shapeNum; idx++) {
        u8 visibility = 0;
        pAnmVisibility->getVisibility(idx, &visibility);

        if (visibility != 0) {
            mModel->getShapePacket(idx)->offFlag(ShapeEntry::Flag_Hidden);
        } else {
            mModel->getShapePacket(idx)->onFlag(ShapeEntry::Flag_Hidden);
        }
    }
}

Clip* BvaPlayer::getAnmVisibility() {
    if (mAnmRes != nullptr) {
        return static_cast< Clip* >(mAnmRes);
    }

    return nullptr;
}
