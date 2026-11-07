#include "Game/Animation/BtpPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"

BtpPlayer::BtpPlayer(const ResourceHolder* pResourceHolder, ModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBtpResTable, pModelData) {
}

void BtpPlayer::apply(Clip* pClip, Model* pModel) {
    const u32 num = pClip->getEntryCount();
    for (u32 i = 0; i < num; i++) {
        if (pClip->hasMaterial(i)) {
            u32 texNo;
            pClip->getTexture(i, &texNo);
            pModel->ownMaterial(pClip->getMaterial(i))->setTextureIndex(pClip->getSlot(i), texNo);
        }
    }
}
