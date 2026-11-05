#include "Game/Animation/BrkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/J3DGraphAnimator/J3DMaterialAttach.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>

BrkPlayer::BrkPlayer(const ResourceHolder* pResourceHolder, J3DModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBrkResTable, pModelData) {
}

void BrkPlayer::attach(J3DAnmBase* pAnmRes, J3DModelData* pModelData) {
    pModelData->mMaterialTable.entryStageColorAnimator(static_cast< J3DAnmStageColorKey* >(pAnmRes));
}

void BrkPlayer::detach(J3DAnmBase* pAnmRes, J3DModelData* pModelData) {
    pModelData->mMaterialTable.removeStageColorAnimator(static_cast< J3DAnmStageColorKey* >(pAnmRes));
}
