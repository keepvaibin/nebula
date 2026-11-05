#include "Game/Animation/BrkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>

BrkPlayer::BrkPlayer(const ResourceHolder* pResourceHolder, J3DModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBrkResTable, pModelData) {
}

void BrkPlayer::apply(J3DAnmBase* pAnmRes, J3DModel* pModel) {
    J3DAnmStageColorKey* pAnm = static_cast< J3DAnmStageColorKey* >(pAnmRes);
    const u16 registers = pAnm->getCRegUpdateMaterialNum();
    for (u16 i = 0; i < registers; i++) {
        const u8 colorId = pAnm->getAnmCRegKeyTable()[i].mColorId;
        if (pAnm->isValidCRegUpdateMaterialID(i) && colorId < 3) {
            pAnm->getStageColorReg(i, pModel->ownMaterial(pAnm->getCRegUpdateMaterialID(i))->getRegisterColor(colorId));
        }
    }
    const u16 constants = pAnm->getKRegUpdateMaterialNum();
    for (u16 i = 0; i < constants; i++) {
        if (pAnm->isValidKRegUpdateMaterialID(i)) {
            const u8 colorId = pAnm->getAnmKRegKeyTable()[i].mColorId;
            pAnm->getStageKonstReg(i, pModel->ownMaterial(pAnm->getKRegUpdateMaterialID(i))->getConstantColor(colorId));
        }
    }
}
