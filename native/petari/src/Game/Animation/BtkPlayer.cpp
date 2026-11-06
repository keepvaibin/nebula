#include "Game/Animation/BtkPlayer.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"

BtkPlayer::BtkPlayer(const ResourceHolder* pResourceHolder, ModelData* pModelData)
    : MaterialAnmPlayerBase(pResourceHolder->mBtkResTable, pModelData) {
}

void BtkPlayer::apply(Clip* pClip, Model* pModel) {
    const u32 num = pClip->getEntryCount();
    for (u32 no = 0; no < num; no++) {
        const u8 texMtxID = pClip->getSlot(no);
        if (!pClip->hasMaterial(no) || texMtxID == 0xFF) {
            continue;
        }
        Material* pMaterial = pModel->ownMaterial(pClip->getMaterial(no));
        if (pMaterial->getTexMtx(texMtxID) == nullptr) {
            pMaterial->setTexMtx(texMtxID, CoordMatrix());
        }
        pMaterial->getTexCoord(texMtxID)->setTexGenMtx(texMtxID);
        CoordMatrixInfo& rInfo = pMaterial->getTexMtx(texMtxID)->getTexMtxInfo();
        rInfo.mInfo = (rInfo.mInfo & 0x3f) | (pClip->getCoordMode() << 7);
        const f32* pCenter = pClip->getCenter(no);
        rInfo.mCenter.x = pCenter[0];
        rInfo.mCenter.y = pCenter[1];
        rInfo.mCenter.z = pCenter[2];
        pClip->getTextureTransform(no, &rInfo.mSRT);
    }
}
