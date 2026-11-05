#include "Game/Animation/BpkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>

BpkPlayer::BpkPlayer(const ResourceHolder* pResourceHolder, J3DModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBpkResTable, pModelData) {
}

void BpkPlayer::apply(J3DAnmBase* pAnmRes, J3DModel* pModel) {
    J3DAnmColor* pAnm = static_cast< J3DAnmColor* >(pAnmRes);
    const u16 num = pAnm->getUpdateMaterialNum();
    for (u16 i = 0; i < num; i++) {
        if (pAnm->isValidUpdateMaterialID(i)) {
            pAnm->getColor(i, pModel->ownMaterial(pAnm->getUpdateMaterialID(i))->getMatColor(0));
        }
    }
}
