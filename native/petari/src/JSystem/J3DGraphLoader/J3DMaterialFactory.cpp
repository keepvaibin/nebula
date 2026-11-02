#include "JSystem/J3DGraphLoader/J3DMaterialFactory.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/JSupport/JSupport.hpp"
#include <nebula_cooked_material.h>

J3DMaterialFactory::J3DMaterialFactory(J3DMaterialBlock const& i_block) {
    mMaterialNum = i_block.mMaterialNum;
    u32 count = 0;
    mpNativeMaterials = nebula_cooked_materials(&i_block, i_block.mBlockSize, &count);
    if (count != mMaterialNum) mpNativeMaterials = nullptr;
    mpMaterialInitData = JSUConvertOffsetToPtr< J3DMaterialInitData >(&i_block, i_block.mpMaterialInitData);
    mpMaterialID = JSUConvertOffsetToPtr< u16 >(&i_block, i_block.mpMaterialID);
    if (i_block.mpIndInitData != 0 && i_block.mpIndInitData - i_block.mpNameTable > 4) {
        mpIndInitData = JSUConvertOffsetToPtr< J3DIndInitData >(&i_block, i_block.mpIndInitData);
    } else {
        mpIndInitData = nullptr;
    }

    mpCullMode = JSUConvertOffsetToPtr< u32 >(&i_block, i_block.mpCullMode);
    mpMatColor = JSUConvertOffsetToPtr< NebulaColor8 >(&i_block, i_block.mpMatColor);
    mpColorChanNum = JSUConvertOffsetToPtr< u8 >(&i_block, i_block.mpColorChanNum);
    mpColorChanInfo = JSUConvertOffsetToPtr< J3DColorChanInfo >(&i_block, i_block.mpColorChanInfo);
    mpAmbColor = JSUConvertOffsetToPtr< NebulaColor8 >(&i_block, i_block.mpAmbColor);
    mpLightInfo = JSUConvertOffsetToPtr< J3DLightInfo >(&i_block, i_block.mpLightInfo);
    mpTexGenNum = JSUConvertOffsetToPtr< u8 >(&i_block, i_block.mpTexGenNum);
    mpTexCoordInfo = JSUConvertOffsetToPtr< J3DTexCoordInfo >(&i_block, i_block.mpTexCoordInfo);
    mpTexCoord2Info = JSUConvertOffsetToPtr< J3DTexCoord2Info >(&i_block, i_block.mpTexCoord2Info);
    mpTexMtxInfo = JSUConvertOffsetToPtr< J3DTexMtxInfo >(&i_block, i_block.mpTexMtxInfo);
    field_0x34 = JSUConvertOffsetToPtr< J3DTexMtxInfo >(&i_block, i_block.field_0x44);
    mpTexNo = JSUConvertOffsetToPtr< u16 >(&i_block, i_block.mpTexNo);
    mpTevColor = JSUConvertOffsetToPtr< NebulaColor16 >(&i_block, i_block.mpTevColor);
    mpTevKColor = JSUConvertOffsetToPtr< NebulaColor8 >(&i_block, i_block.mpTevKColor);
    mpTevStageNum = JSUConvertOffsetToPtr< u8 >(&i_block, i_block.mpTevStageNum);
    mpFogInfo = JSUConvertOffsetToPtr< J3DFogInfo >(&i_block, i_block.mpFogInfo);
    mpAlphaCompInfo = JSUConvertOffsetToPtr< J3DAlphaCompInfo >(&i_block, i_block.mpAlphaCompInfo);
    mpBlendInfo = JSUConvertOffsetToPtr< J3DBlendInfo >(&i_block, i_block.mpBlendInfo);
    mpZModeInfo = JSUConvertOffsetToPtr< J3DZModeInfo >(&i_block, i_block.mpZModeInfo);
    mpZCompLoc = JSUConvertOffsetToPtr< u8 >(&i_block, i_block.mpZCompLoc);
    mpDither = JSUConvertOffsetToPtr< u8 >(&i_block, i_block.mpDither);
    mpNBTScaleInfo = JSUConvertOffsetToPtr< J3DNBTScaleInfo >(&i_block, i_block.mpNBTScaleInfo);
    mpShapeDrawInit = nullptr;
    mpPatchingInfo = nullptr;
    mpCurrentMtxInfo = nullptr;
    mpMaterialMode = nullptr;
}

u16 J3DMaterialFactory::countUniqueMaterials() {
    u16 count = 0;
    s32 id = -1;
    for (u16 i = 0; i < mMaterialNum; i++) {
        if (id < mpMaterialID[i]) {
            id = mpMaterialID[i];
            count++;
        }
    }

    return count;
}

u32 J3DMaterialFactory::countTexGens(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTexGenNumIdx != 0xff) {
        return mpTexGenNum[mtl_init_data->mTexGenNumIdx];
    }

    return 0;
}

u32 J3DMaterialFactory::countStages(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    u32 count1 = 0;
    u32 count2 = 0;
    if (mtl_init_data->mTevStageNumIdx != 0xff) {
        count2 = mpTevStageNum[mtl_init_data->mTevStageNumIdx];
    }

    for (int i = 0; i < 8; i++) {
        if (mtl_init_data->mTexNoIdx[i] != 0xffff) {
            count1++;
        }
    }

    if (count2 != count1 && count1 != 0) {
        u32 count3;
        if (count2 > count1) {
            count3 = count2;
        } else {
            count3 = count1;
        }

        return count3;
    }

    return count2;
}

J3DMaterial* J3DMaterialFactory::create(J3DMaterial* i_material, MaterialType i_type, int i_idx, u32 i_flags) const {
    switch (i_type) {
    case MATERIAL_TYPE_NORMAL:
        i_material = createNormalMaterial(i_material, i_idx, i_flags);
        break;
    case MATERIAL_TYPE_PATCHED:
        i_material = createPatchedMaterial(i_material, i_idx, i_flags);
        break;
    }

    return i_material;
}

J3DMaterial* J3DMaterialFactory::createNormalMaterial(J3DMaterial* i_material, int i_idx, u32 i_flags) const {
    const u32 stages = countStages(i_idx);
    u32 tev_stage_num = getMdlDataFlag_TevStageNum(i_flags);
    u32 tev_stage_num_max = (stages > tev_stage_num ? stages : tev_stage_num);
    u32 tex_num = tev_stage_num_max > 8 ? 8 : tev_stage_num_max;
    u32 texgens = countTexGens(i_idx);
    u32 texgen_flag = texgens > 4 ? getMdlDataFlag_TexGenFlag(0) : getMdlDataFlag_TexGenFlag(i_flags);
    u32 color_flag = getMdlDataFlag_ColorFlag(i_flags);
    u32 pe_flag = getMdlDataFlag_PEFlag(i_flags);
    BOOL ind_flag = (i_flags & 0x1000000) ? TRUE : FALSE;
    if (i_material == nullptr) {
        i_material = new J3DMaterial();
    }

    i_material->mColorBlock = J3DMaterial::createColorBlock(color_flag);
    i_material->mTexGenBlock = J3DMaterial::createTexGenBlock(texgen_flag);
    i_material->mTevBlock = J3DMaterial::createTevBlock((u16)tev_stage_num_max);
    i_material->mIndBlock = J3DMaterial::createIndBlock(ind_flag);
    i_material->mPEBlock = J3DMaterial::createPEBlock(pe_flag, getMaterialMode(i_idx));
    i_material->mIndex = i_idx;
    i_material->mNativeMaterial = mpNativeMaterials != nullptr ? mpNativeMaterials + i_idx : nullptr;
    i_material->mMaterialMode = getMaterialMode(i_idx);
    i_material->mColorBlock->setColorChanNum(newColorChanNum(i_idx));
    i_material->mColorBlock->setCullMode(newCullMode(i_idx));
    i_material->mTexGenBlock->setTexGenNum(newTexGenNum(i_idx));
    i_material->mTexGenBlock->setNBTScale(newNBTScale(i_idx));
    i_material->mPEBlock->setFog(newFog(i_idx));
    i_material->mPEBlock->setAlphaComp(newAlphaComp(i_idx));
    i_material->mPEBlock->setBlend(newBlend(i_idx));
    i_material->mPEBlock->setZMode(newZMode(i_idx));
    i_material->mPEBlock->setZCompLoc(newZCompLoc(i_idx));
    for (u8 i = 0; i < tex_num; i++) {
        i_material->mTevBlock->setTexNo(i, newTexNo(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        i_material->mTevBlock->setTevKColor(i, newTevKColor(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        i_material->mTevBlock->setTevColor(i, newTevColor(i_idx, i));
    }

    for (u8 i = 0; i < 2; i++) {
        i_material->mColorBlock->setAmbColor(i, newAmbColor(i_idx, i));
    }

    for (u8 i = 0; i < 2; i++) {
        i_material->mColorBlock->setMatColor(i, newMatColor(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        J3DColorChan color_chan = newColorChan(i_idx, i);
        i_material->mColorBlock->setColorChan(i, color_chan);
    }

    for (u8 i = 0; i < texgens; i++) {
        J3DTexCoord tex_coord = newTexCoord(i_idx, i);
        i_material->mTexGenBlock->setTexCoord(i, &tex_coord);
    }

    for (u8 i = 0; i < 8; i++) {
        i_material->mTexGenBlock->setTexMtx(i, newTexMtx(i_idx, i));
    }

    if (mpIndInitData != nullptr) {
        u8 ind_tex_stage_num = newIndTexStageNum(i_idx);
        i_material->mIndBlock->setIndTexStageNum(newIndTexStageNum(i_idx));
        for (u8 i = 0; i < ind_tex_stage_num; i++) {
            i_material->mIndBlock->setIndTexMtx(i, newIndTexMtx(i_idx, i));
        }

    }

    return i_material;
}

J3DMaterial* J3DMaterialFactory::createPatchedMaterial(J3DMaterial* i_material, int i_idx, u32 i_flags) const {
    if (i_material == nullptr) {
        i_material = new J3DPatchedMaterial();
    }
    i_material->mNativeMaterial = mpNativeMaterials != nullptr ? mpNativeMaterials + i_idx : nullptr;

    u8 indFlag = (i_flags & 0x3000000) != 0 ? static_cast< u8 >(1) : static_cast< u8 >(0);
    i_material->mColorBlock = J3DMaterial::createColorBlock(0x40000000);
    i_material->mTexGenBlock = new J3DTexGenBlock(8, false);
    i_material->mTevBlock = new J3DTevBlock(8, true, true);
    i_material->mIndBlock = J3DMaterial::createIndBlock(indFlag);
    i_material->mPEBlock = J3DMaterial::createPEBlock(0x10000000, getMaterialMode(i_idx));
    i_material->mIndex = i_idx;
    i_material->mMaterialMode = getMaterialMode(i_idx);
    i_material->mColorBlock->setColorChanNum(newColorChanNum(i_idx));
    i_material->mColorBlock->setCullMode(newCullMode(i_idx));
    i_material->mPEBlock->setFog(newFog(i_idx));
    i_material->mPEBlock->setAlphaComp(newAlphaComp(i_idx));
    i_material->mPEBlock->setBlend(newBlend(i_idx));
    i_material->mPEBlock->setZMode(newZMode(i_idx));
    i_material->mPEBlock->setZCompLoc(newZCompLoc(i_idx));
    for (u8 i = 0; i < 8; i++) {
        i_material->mTevBlock->setTexNo(i, newTexNo(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        i_material->mTevBlock->setTevKColor(i, newTevKColor(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        i_material->mTevBlock->setTevColor(i, newTevColor(i_idx, i));
    }

    for (u8 i = 0; i < 2; i++) {
        i_material->mColorBlock->setMatColor(i, newMatColor(i_idx, i));
    }

    for (u8 i = 0; i < 4; i++) {
        J3DColorChan color_chan = newColorChan(i_idx, i);
        i_material->mColorBlock->setColorChan(i, color_chan);
    }

    u32 texgens = countTexGens(i_idx);
    i_material->mTexGenBlock->setTexGenNum(newTexGenNum(i_idx));
    for (u8 i = 0; i < 8; i++) {
        i_material->mTexGenBlock->setTexMtx(i, newTexMtx(i_idx, i));
    }

    for (u8 i = 0; i < texgens; i++) {
        J3DTexCoord tex_coord = newTexCoord(i_idx, i);
        i_material->mTexGenBlock->setTexCoord(i, &tex_coord);
    }

    if (indFlag && mpIndInitData != nullptr) {
        u8 ind_tex_stage_num = newIndTexStageNum(i_idx);
        i_material->mIndBlock->setIndTexStageNum(newIndTexStageNum(i_idx));
        for (u8 i = 0; i < ind_tex_stage_num; i++) {
            i_material->mIndBlock->setIndTexMtx(i, newIndTexMtx(i_idx, i));
        }

    }

    return i_material;
}

void J3DMaterialFactory::modifyPatchedCurrentMtx(J3DMaterial* i_material, int i_idx) const {
    J3DTexCoord coord[8];
    u32 tex_gens = countTexGens(i_idx);
    for (u8 i = 0; i < tex_gens; i++) {
        coord[i].mInfo = newTexCoord(i_idx, i).mInfo;
    }

    J3DCurrentMtx currentMtx;
    currentMtx.setCurrentTexMtx(coord[0].getTexGenMtx(), coord[1].getTexGenMtx(), coord[2].getTexGenMtx(), coord[3].getTexGenMtx(),
                                coord[4].getTexGenMtx(), coord[5].getTexGenMtx(), coord[6].getTexGenMtx(), coord[7].getTexGenMtx());
    i_material->mCurrentMtx = currentMtx;
}

J3DGXColor J3DMaterialFactory::newMatColor(int i_idx, int i_no) const {
    J3DGXColor dflt = (NebulaColor8){0xff, 0xff, 0xff, 0xff};
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mMatColorIdx[i_no] != 0xffff) {
        return mpMatColor[mtl_init_data->mMatColorIdx[i_no]];
    } else {
        return dflt;
    }
}

const u8 J3DMaterialFactory::newColorChanNum(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mColorChanNumIdx != 0xff) {
        return mpColorChanNum[mtl_init_data->mColorChanNumIdx];
    } else {
        return 0;
    }
}

J3DColorChan J3DMaterialFactory::newColorChan(int i_idx, int i_no) const {
    u32 r29 = 0;
    J3DMaterialInitData* init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (init_data->mColorChanIdx[i_no] != 0xffff) {
        return J3DColorChan(mpColorChanInfo[init_data->mColorChanIdx[i_no]]);
    } else {
        return J3DColorChan();
    }
}

J3DGXColor J3DMaterialFactory::newAmbColor(int i_idx, int i_no) const {
    J3DGXColor dflt = (NebulaColor8){0x32, 0x32, 0x32, 0x32};
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mAmbColorIdx[i_no] != 0xffff) {
        return mpAmbColor[mtl_init_data->mAmbColorIdx[i_no]];
    } else {
        return dflt;
    }
}

u32 J3DMaterialFactory::newTexGenNum(int i_idx) const {
    u32 r30 = 0;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTexGenNumIdx != 0xff) {
        return mpTexGenNum[mtl_init_data->mTexGenNumIdx];
    } else {
        return 0;
    }
}

J3DTexCoord J3DMaterialFactory::newTexCoord(int i_idx, int i_no) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTexCoordIdx[i_no] != 0xffff) {
        return J3DTexCoord(mpTexCoordInfo[mtl_init_data->mTexCoordIdx[i_no]]);
    } else {
        return J3DTexCoord();
    }
}

J3DTexMtx* J3DMaterialFactory::newTexMtx(int i_idx, int i_no) const {
    J3DTexMtx* tex_mtx = nullptr;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTexMtxIdx[i_no] != 0xffff) {
        tex_mtx = new J3DTexMtx(mpTexMtxInfo[mtl_init_data->mTexMtxIdx[i_no]]);
    }

    return tex_mtx;
}

u8 J3DMaterialFactory::newCullMode(int i_idx) const {
    u32 r30 = 0;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mCullModeIdx != 0xff) {
        return mpCullMode[mtl_init_data->mCullModeIdx];
    } else {
        return 0xff;
    }
}

u16 J3DMaterialFactory::newTexNo(int i_idx, int i_no) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTexNoIdx[i_no] != 0xffff) {
        return mpTexNo[mtl_init_data->mTexNoIdx[i_no]];
    } else {
        return 0xffff;
    }
}

J3DGXColorS10 J3DMaterialFactory::newTevColor(int i_idx, int i_no) const {
    NebulaColor16 _dflt = {0, 0, 0, 0};
    J3DGXColorS10 dflt = _dflt;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTevColorIdx[i_no] != 0xffff) {
        return J3DGXColorS10(mpTevColor[mtl_init_data->mTevColorIdx[i_no]]);
    } else {
        return dflt;
    }
}

J3DGXColor J3DMaterialFactory::newTevKColor(int i_idx, int i_no) const {
    J3DGXColor dflt = (NebulaColor8){0xff, 0xff, 0xff, 0xff};
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mTevKColorIdx[i_no] != 0xffff) {
        return mpTevKColor[mtl_init_data->mTevKColorIdx[i_no]];
    } else {
        return dflt;
    }
}

u8 J3DMaterialFactory::newIndTexStageNum(int i_idx) const {
    u8 dflt = 0;
    if (mpIndInitData[i_idx].mEnabled == true) {
        return mpIndInitData[i_idx].mIndTexStageNum;
    } else {
        return dflt;
    }
}

J3DIndTexMtx J3DMaterialFactory::newIndTexMtx(int i_idx, int i_no) const {
    J3DIndTexMtx dflt;
    if (mpIndInitData[i_idx].mEnabled == true) {
        return J3DIndTexMtx(mpIndInitData[i_idx].mIndTexMtxInfo[i_no]);
    } else {
        return dflt;
    }
}

J3DFog J3DMaterialFactory::newFog(int i_idx) const {
    J3DFog fog;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mFogIdx != 0xffff) {
        fog.setFogInfo(mpFogInfo[mtl_init_data->mFogIdx]);
    }

    return fog;
}

J3DAlphaComp J3DMaterialFactory::newAlphaComp(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mAlphaCompIdx != 0xffff) {
        return J3DAlphaComp(mpAlphaCompInfo[mtl_init_data->mAlphaCompIdx]);
    } else {
        return J3DAlphaComp();
    }
}

J3DBlend J3DMaterialFactory::newBlend(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mBlendIdx != 0xffff) {
        return J3DBlend(mpBlendInfo[mtl_init_data->mBlendIdx]);
    } else {
        return J3DBlend(j3dDefaultBlendInfo);
    }
}

J3DZMode J3DMaterialFactory::newZMode(int i_idx) const {
    u32 r29 = 0;
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mZModeIdx != 0xff) {
        return J3DZMode(mpZModeInfo[mtl_init_data->mZModeIdx]);
    } else {
        return J3DZMode();
    }
}

const u8 J3DMaterialFactory::newZCompLoc(int i_idx) const {
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mZCompLocIdx != 0xff) {
        return mpZCompLoc[mtl_init_data->mZCompLocIdx];
    } else {
        return 0;
    }
}

J3DNBTScale J3DMaterialFactory::newNBTScale(int i_idx) const {
    J3DNBTScale dflt(j3dDefaultNBTScaleInfo);
    J3DMaterialInitData* mtl_init_data = &mpMaterialInitData[mpMaterialID[i_idx]];
    if (mtl_init_data->mNBTScaleIdx != 0xffff) {
        return J3DNBTScale(mpNBTScaleInfo[mtl_init_data->mNBTScaleIdx]);
    } else {
        return dflt;
    }
}
