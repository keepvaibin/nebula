#include "Game/Animation/BpkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"

BpkPlayer::BpkPlayer(const ResourceHolder* pResourceHolder, ModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBpkResTable, pModelData) {
}

void BpkPlayer::apply(Clip* pClip, Model* pModel) {
    const u32 num = pClip->getEntryCount();
    for (u32 i = 0; i < num; i++) {
        if (pClip->hasMaterial(i)) {
            pClip->getColor(i, pModel->ownMaterial(pClip->getMaterial(i))->getMaterialColor(0));
        }
    }
}
