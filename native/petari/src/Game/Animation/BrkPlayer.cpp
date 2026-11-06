#include "Game/Animation/BrkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"

BrkPlayer::BrkPlayer(const ResourceHolder* pResourceHolder, ModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBrkResTable, pModelData) {
}

void BrkPlayer::apply(Clip* pClip, Model* pModel) {
    const u32 registers = pClip->getRegisterCount();
    for (u32 i = 0; i < registers; i++) {
        const u8 slot = pClip->getSlot(i);
        if (pClip->hasMaterial(i) && slot < 3) {
            pClip->getRegister(i, pModel->ownMaterial(pClip->getMaterial(i))->getRegisterColor(slot));
        }
    }
    const u32 constants = pClip->getConstantCount();
    for (u32 i = 0; i < constants; i++) {
        const u32 material = pClip->getConstantMaterial(i);
        if (material != NEBULA_NONE32) {
            pClip->getConstant(i, pModel->ownMaterial(material)->getConstantColor(pClip->getConstantSlot(i)));
        }
    }
}
