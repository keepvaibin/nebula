#include "Game/Animation/BtpPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>

BtpPlayer::BtpPlayer(const ResourceHolder* pResourceHolder, J3DModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBtpResTable, pModelData) {
}

void BtpPlayer::apply(J3DAnmBase* pAnmRes, J3DModel* pModel) {
    J3DAnmTexPattern* pAnm = static_cast< J3DAnmTexPattern* >(pAnmRes);
    const u16 num = pAnm->getUpdateMaterialNum();
    for (u16 i = 0; i < num; i++) {
        if (pAnm->isValidUpdateMaterialID(i)) {
            u16 texNo;
            pAnm->getTexNo(i, &texNo);
            pModel->ownMaterial(pAnm->getUpdateMaterialID(i))->setTexNo(pAnm->getAnmTable()[i].mTexNo, texNo);
        }
    }
}
