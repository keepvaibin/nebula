#include "Game/Animation/BtkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>

BtkPlayer::BtkPlayer(const ResourceHolder* pResourceHolder, J3DModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBtkResTable, pModelData) {
}

void BtkPlayer::apply(J3DAnmBase* pAnmRes, J3DModel* pModel) {
    J3DAnmTextureSRTKey* pAnm = static_cast< J3DAnmTextureSRTKey* >(pAnmRes);
    const u16 num = pAnm->getUpdateMaterialNum();
    for (u16 no = 0; no < num; no++) {
        const u8 texMtxID = pAnm->getUpdateTexMtxID(no);
        if (!pAnm->isValidUpdateMaterialID(no) || texMtxID == 0xFF) {
            continue;
        }
        J3DMaterial* pMaterial = pModel->ownMaterial(pAnm->getUpdateMaterialID(no));
        if (pMaterial->getTexMtx(texMtxID) == nullptr) {
            pMaterial->setTexMtx(texMtxID, J3DTexMtx());
        }
        pMaterial->getTexCoord(texMtxID)->setTexGenMtx(static_cast< u8 >(texMtxID * 3 + 30));
        J3DTexMtxInfo& rInfo = pMaterial->getTexMtx(texMtxID)->getTexMtxInfo();
        rInfo.mInfo = (rInfo.mInfo & 0x3f) | (pAnm->getTexMtxCalcType() << 7);
        rInfo.mCenter.x = pAnm->getSRTCenter(no)->x;
        rInfo.mCenter.y = pAnm->getSRTCenter(no)->y;
        rInfo.mCenter.z = pAnm->getSRTCenter(no)->z;
        pAnm->getTransform(no, &rInfo.mSRT);
    }
}
