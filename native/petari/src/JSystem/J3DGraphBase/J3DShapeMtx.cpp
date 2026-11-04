#include <string.h>
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"

extern "C" const NebulaHostServices* nebula_host(void);

void J3DPSMtx33CopyFrom34(MtxPtr src, Mtx3P dst);

MtxPtr J3DShapeMtxConcatView::sMtxPtrTbl[2];

void J3DDifferedTexMtx::loadExecute(f32 const (*param_0)[4]) {
    static Mtx qMtx = {
        {0.5f, 0.0f, 0.5f, 0.0f},
        {0.0f, -0.5f, 0.5f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };

    static Mtx qMtx2 = {{0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f}};

    Mtx* mtx;  // sp_64
    Mtx sp_e8;
    Mtx44 sp_a8, sp_68;

    J3DTexGenBlock* tex_gen_block = sTexGenBlock;  // sp_60

    J3DTexMtxObj* tex_mtx_obj = sTexMtxObj;  // sp_5c

    J3DTexMtxInfo* tex_mtx_info_1;  // sp_58
    u8 tex_gen_type;      // sp_54
    J3DTexMtx* tex_mtx_2;           // sp_50

    u16 tex_mtx_num = tex_mtx_obj->getNumTexMtx();

    if (j3dSys.checkFlag(J3DSysFlag_PostTexMtx)) {
        for (u16 i = 0; i < tex_mtx_num; i++) {
            tex_gen_type = static_cast< u8 >(tex_gen_block->getTexCoord(i)->getTexGenType());
            if (tex_gen_type != 1 && tex_gen_type != 0) {
                continue;
            }
            {
                tex_mtx_2 = tex_gen_block->getTexMtx(i);
                tex_mtx_info_1 = &tex_mtx_2->getTexMtxInfo();

                u32 sp_4c = tex_mtx_info_1->mInfo & 0x3f;
                switch (sp_4c) {
                case 3:
                case 9: {
                    mtx = &tex_mtx_obj->getMtx(i);
                    break;
                }
                case 1:
                case 6:
                case 7: {
                    mtx = &tex_mtx_obj->getMtx(i);
                    break;
                }
                case 2:
                case 8: {
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_e8);
                    NebulaMtxConcat(tex_mtx_obj->getMtx(i), sp_e8, sp_e8);
                    mtx = &sp_e8;
                    break;
                }
                case 5: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx_2->getTexMtxInfo();  // sp_48
                    u32 sp_44 = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_44 == 0) {
                        J3DGetTextureMtxOld(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_44 == 1) {
                        J3DGetTextureMtxMayaOld(tex_mtx_info_2->mSRT, sp_68);
                    }
                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    sp_e8[0][3] = sp_e8[1][3] = sp_e8[2][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 11: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx_2->getTexMtxInfo();  // sp_40
                    u32 sp_3c = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_3c == 0) {
                        J3DGetTextureMtx(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_3c == 1) {
                        J3DGetTextureMtxMaya(tex_mtx_info_2->mSRT, sp_68);
                    }

                    NebulaMtxConcat(sp_68, qMtx, sp_68);
                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    sp_e8[0][3] = sp_e8[1][3] = sp_e8[2][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 10: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx_2->getTexMtxInfo();  // sp_38
                    u32 sp_34 = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_34 == 0) {
                        J3DGetTextureMtx(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_34 == 1) {
                        J3DGetTextureMtxMaya(tex_mtx_info_2->mSRT, sp_68);
                    }

                    NebulaMtxConcat(sp_68, qMtx2, sp_68);
                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    sp_e8[0][3] = sp_e8[1][3] = sp_e8[2][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 0:
                case 4:
                default: {
                    mtx = &tex_mtx_obj->getMtx(i);
                }
                }
                J3DNativeShape::setPostTexture(i, *mtx);
            }
        }
    } else {
        for (u16 i = 0; i < tex_mtx_num; i++) {
            u8 tex_gen_type = static_cast< u8 >(tex_gen_block->getTexCoord(i)->getTexGenType());
            if (tex_gen_type != 1 && tex_gen_type != 0) {
                continue;
            }
            {
                J3DTexMtx* tex_mtx = tex_gen_block->getTexMtx(i);  // sp_2c
                tex_mtx_info_1 = &tex_mtx->getTexMtxInfo();

                u32 tex_gen_src = tex_mtx_info_1->mInfo & 0x3f;  // sp_28
                switch (tex_gen_src) {
                case 3:
                case 9:
                    NebulaMtxConcat(tex_mtx_obj->getMtx(i), param_0, sp_e8);
                    mtx = &sp_e8;
                    break;
                case 1:
                case 6:
                case 7:
                    NebulaMtxCopy(param_0, sp_a8);
                    sp_a8[0][3] = sp_a8[1][3] = sp_a8[2][3] = 0.0f;
                    NebulaMtxConcat(tex_mtx_obj->getMtx(i), sp_a8, sp_e8);
                    mtx = &sp_e8;
                    break;
                case 2:
                case 8:
                    mtx = &tex_mtx_obj->getMtx(i);
                    break;
                case 5: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx->getTexMtxInfo();  // sp_24
                    u32 sp_24 = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_24 == 0) {
                        J3DGetTextureMtxOld(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_24 == 1) {
                        J3DGetTextureMtxMayaOld(tex_mtx_info_2->mSRT, sp_68);
                    }

                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    NebulaMtxConcat(sp_e8, param_0, sp_e8);
                    sp_e8[0][3] = sp_e8[1][3] = sp_e8[2][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 11: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx->getTexMtxInfo();  // sp_1c
                    u32 sp_18 = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_18 == 0) {
                        J3DGetTextureMtx(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_18 == 1) {
                        J3DGetTextureMtxMaya(tex_mtx_info_2->mSRT, sp_68);
                    }

                    NebulaMtxConcat(sp_68, qMtx, sp_68);
                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    NebulaMtxConcat(sp_e8, param_0, sp_e8);
                    sp_e8[0][3] = sp_e8[1][3] = sp_e8[2][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 10: {
                    J3DTexMtxInfo* tex_mtx_info_2 = &tex_mtx->getTexMtxInfo();  // sp_14
                    u32 sp_10 = (u32)(tex_mtx_info_2->mInfo & 0x80) >> 7;
                    if (sp_10 == 0) {
                        J3DGetTextureMtx(tex_mtx_info_2->mSRT, tex_mtx_info_2->mCenter, sp_68);
                    } else if (sp_10 == 1) {
                        J3DGetTextureMtxMaya(tex_mtx_info_2->mSRT, sp_68);
                    }

                    NebulaMtxConcat(sp_68, qMtx2, sp_68);
                    J3DMtxProjConcat(sp_68, tex_mtx_obj->getEffectMtx(i), sp_e8);
                    NebulaMtxInverse(j3dSys.getViewMtx(), sp_a8);
                    NebulaMtxConcat(sp_e8, sp_a8, sp_e8);
                    NebulaMtxConcat(sp_e8, param_0, sp_e8);
                    sp_e8[2][3] = 0.0f;
                    sp_e8[1][3] = 0.0f;
                    sp_e8[0][3] = 0.0f;
                    mtx = &sp_e8;
                    break;
                }
                case 0:
                case 4:
                default: {
                    mtx = &tex_mtx_obj->getMtx(i);
                    break;
                }
                }
                J3DNativeShape::setTexture(i, *mtx, tex_mtx_info_1->mProjection == J3D_MTX2x4 ? 2 : 3);
            }
        }
    }
}

void J3DShapeMtxConcatView::loadMtxConcatView_PNGP(int slot, u16 drw) const {
    Mtx m;
    NebulaMtxConcat(*j3dSys.getShapePacket()->getBaseMtxPtr(), j3dSys.getModelDrawMtx(drw), m);
    J3DDifferedTexMtx::load(m);
    J3DNativeShape::setPosition(slot, m);
    loadNrmMtx(slot, drw, m);
}

u32 J3DShapeMtxConcatView::loadPalette() {
    J3DModel* pModel = j3dSys.getModel();
    const f32* pBase = &(*j3dSys.getShapePacket()->getBaseMtxPtr())[0][0];
    const Vec* pScale = sNBTFlag ? j3dSys.getNBTScale() : nullptr;
    const Vec scale = pScale != nullptr ? *pScale : Vec{1.0f, 1.0f, 1.0f};
    const u32 frame = NebulaFrameClock::count();
    for (u32 i = 0; i < 2; i++) {
        const J3DModel::NebulaPalette& rKept = pModel->mNebulaPalette[i];
        if (rKept.mId != 0 && rKept.mFrame == frame && rKept.mGeneration == pModel->mNebulaGeneration &&
            memcmp(rKept.mBase, pBase, sizeof(rKept.mBase)) == 0 && rKept.mScale.x == scale.x &&
            rKept.mScale.y == scale.y && rKept.mScale.z == scale.z) {
            return rKept.mId;
        }
    }
    J3DModelData* pData = pModel->getModelData();
    const u32 count = pData->getDrawMtxNum();
    f32* pRows = nullptr;
    const NebulaHostServices* pHost = nebula_host();
    const u32 id = pHost->shape_palette(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pModel)), count, frame,
                                        &pRows);
    MtxPtr tables[2] = {pModel->getMtxBuffer()->getUserAnmMtx(0), pModel->getWeightAnmMtx(0)};
    f32* pNormal = pRows + 12 * count;
    for (u32 k = 0; k < count; k++, pRows += 12, pNormal += 12) {
        const u16 index = pData->getDrawMtxIndex(k);
        Mtx m;
        NebulaMtxConcat(*reinterpret_cast< const Mtx* >(pBase), reinterpret_cast< const Mtx* >(tables[pData->getDrawMtxFlag(k)])[index], m);
        memcpy(pRows, m, sizeof(m));
        Mtx33 n;
        if (sCurrentScaleFlag[index] == 1) {
            J3DPSMtx33CopyFrom34(m, n);
        } else {
            J3DPSCalcInverseTranspose(m, n);
        }
        if (pScale != nullptr) {
            J3DScaleNrmMtx33(n, scale);
        }
        for (u32 r = 0; r < 3; r++) {
            pNormal[r * 4 + 0] = n[r][0];
            pNormal[r * 4 + 1] = n[r][1];
            pNormal[r * 4 + 2] = n[r][2];
            pNormal[r * 4 + 3] = 0.0f;
        }
    }
    J3DModel::NebulaPalette& rSlot = pModel->mNebulaPalette[pModel->mNebulaPaletteNext];
    pModel->mNebulaPaletteNext ^= 1;
    rSlot.mId = id;
    rSlot.mFrame = frame;
    rSlot.mGeneration = pModel->mNebulaGeneration;
    memcpy(rSlot.mBase, pBase, sizeof(rSlot.mBase));
    rSlot.mScale = scale;
    return id;
}

void J3DShapeMtxConcatView::loadTextureInputs(int slot, u16 index) const {
    Mtx m;
    NebulaMtxConcat(*j3dSys.getShapePacket()->getBaseMtxPtr(), j3dSys.getModelDrawMtx(index), m);
    J3DDifferedTexMtx::load(m);
    if (sTexMtxLoadType == 0x2000) {
        if (sCurrentScaleFlag[index] == 1) {
            J3DNativeShape::setNormalTexture(slot, m);
        } else {
            Mtx33 n;
            J3DPSCalcInverseTranspose(m, n);
            J3DNativeShape::setNormal33Texture(slot, n);
        }
    }
}

void J3DShapeMtx::load() const {
    loadMtxIndx_PNGP(0, mUseMtxIndex);
}

void J3DPSMtx33Copy(Mtx3P, Mtx3P);

void J3DShapeMtx::calcNBTScale(Vec const& param_0, Mtx33* param_1, Mtx33* param_2) {
    J3DPSMtx33Copy(param_1[mUseMtxIndex], param_2[mUseMtxIndex]);
    J3DScaleNrmMtx33(param_2[mUseMtxIndex], param_0);
}

void J3DShapeMtxConcatView::load() const {
    sMtxPtrTbl[0] = j3dSys.getModel()->getMtxBuffer()->getUserAnmMtx(0);
    sMtxPtrTbl[1] = j3dSys.getModel()->getWeightAnmMtx(0);
    J3DNativeShape::setPalette(loadPalette(), mUseMtxIndex);
    if (J3DDifferedTexMtx::sTexGenBlock != nullptr || sTexMtxLoadType == 0x2000) {
        j3dSys.setModelDrawMtx((Mtx*)sMtxPtrTbl[j3dSys.getModel()->getModelData()->getDrawMtxFlag(mUseMtxIndex)]);
        loadTextureInputs(0, j3dSys.getModel()->getModelData()->getDrawMtxIndex(mUseMtxIndex));
    }
}

void J3DShapeMtxConcatView::loadNrmMtx(int param_0, u16 param_1, MtxPtr param_2) const {
    if (sCurrentScaleFlag[param_1] == 1) {
        if (sTexMtxLoadType == 0x2000) {
            J3DNativeShape::setNormalTexture(0, param_2);
        }

        if (!sNBTFlag) {
            J3DNativeShape::setNormal(0, param_2);
        } else {
            Mtx33 mtx;
            Vec* scale = j3dSys.getNBTScale();
            J3DPSMtx33CopyFrom34(param_2, mtx);
            J3DScaleNrmMtx33(mtx, *scale);
            J3DNativeShape::setNormal33(0, mtx);
        }
    } else {
        Mtx33 mtx;
        J3DPSCalcInverseTranspose(param_2, mtx);
        if (sTexMtxLoadType == 0x2000) {
            J3DNativeShape::setNormal33Texture(0, mtx);
        }

        if (!sNBTFlag) {
            J3DNativeShape::setNormal33(0, mtx);
        } else {
            Vec* scale = j3dSys.getNBTScale();
            J3DScaleNrmMtx33(mtx, *scale);
            J3DNativeShape::setNormal33(0, mtx);
        }
    }
}

void J3DShapeMtxMulti::load() const {
    int use_mtx_num = mUseMtxNum;
    for (int i = 0; i < use_mtx_num; i++) {
        if (mUseMtxIndexTable[i] != 0xffff) {
            loadMtxIndx_PNGP(i, mUseMtxIndexTable[i]);
        }
    }
}

void J3DShapeMtxMulti::calcNBTScale(Vec const& param_0, Mtx33* param_1, Mtx33* param_2) {
    int use_mtx_num = mUseMtxNum;
    for (int i = 0; i < use_mtx_num; i++) {
        if (mUseMtxIndexTable[i] != 0xffff) {
            J3DPSMtx33Copy(param_1[mUseMtxIndexTable[i]], param_2[mUseMtxIndexTable[i]]);
            J3DScaleNrmMtx33(param_2[mUseMtxIndexTable[i]], param_0);
        }
    }
}

void J3DShapeMtxMultiConcatView::load() const {
    sMtxPtrTbl[0] = j3dSys.getModel()->getMtxBuffer()->getUserAnmMtx(0);
    sMtxPtrTbl[1] = j3dSys.getModel()->getWeightAnmMtx(0);
    J3DNativeShape::setPalette(loadPalette(), 0);
    // A coordinate's matrix made from a position matrix: the group's last, as it always read.
    if (J3DDifferedTexMtx::sTexGenBlock != nullptr) {
        for (int i = 0; i < mUseMtxNum; i++) {
            if (mUseMtxIndexTable[i] != 0xffff) {
                j3dSys.setModelDrawMtx((Mtx*)sMtxPtrTbl[j3dSys.getModel()->getModelData()->getDrawMtxFlag(mUseMtxIndexTable[i])]);
                Mtx m;
                NebulaMtxConcat(*j3dSys.getShapePacket()->getBaseMtxPtr(),
                                j3dSys.getModelDrawMtx(j3dSys.getModel()->getModelData()->getDrawMtxIndex(mUseMtxIndexTable[i])), m);
                J3DDifferedTexMtx::load(m);
            }
        }
    }
}

void J3DShapeMtxMultiConcatView::loadNrmMtx(int param_0, u16 param_1, MtxPtr param_2) const {
    Mtx33 mtx1, mtx2;
    if (sCurrentScaleFlag[param_1] == 1) {
        if (sTexMtxLoadType == 0x2000) {
            J3DNativeShape::setNormalTexture(param_0, param_2);
        }
        if (!sNBTFlag) {
            J3DNativeShape::setNormal(param_0, param_2);
        } else {
            Vec* nbt_scale = j3dSys.getNBTScale();
            J3DPSMtx33CopyFrom34(param_2, mtx1);
            J3DScaleNrmMtx33(mtx1, *nbt_scale);
            J3DNativeShape::setNormal33(0, mtx1);
        }
    } else {
        J3DPSCalcInverseTranspose(param_2, mtx2);
        if (sTexMtxLoadType == 0x2000) {
            J3DNativeShape::setNormal33Texture(param_0, mtx2);
        }
        if (!sNBTFlag) {
            J3DNativeShape::setNormal33(param_0, mtx2);
        } else {
            Vec* nbt_scale = j3dSys.getNBTScale();
            J3DScaleNrmMtx33(mtx2, *nbt_scale);
            J3DNativeShape::setNormal33(param_0, mtx2);
        }
    }
}

void J3DShapeMtxBBoardConcatView::load() const {
    Mtx mtx;
    u16 draw_mtx_index = j3dSys.getModel()->getModelData()->getDrawMtxIndex(mUseMtxIndex);

    if (j3dSys.getModel()->getModelData()->getDrawMtxFlag(mUseMtxIndex) == 0) {
        NebulaMtxConcat(j3dSys.getViewMtx(), j3dSys.getModel()->getMtxBuffer()->getUserAnmMtx(draw_mtx_index), mtx);
    } else {
        NebulaMtxConcat(j3dSys.getViewMtx(), j3dSys.getModel()->getWeightAnmMtx(draw_mtx_index), mtx);
    }

    J3DCalcBBoardMtx(mtx);
    J3DNativeShape::setPosition(0, mtx);

    mtx[0][0] = 1.0f / mtx[0][0];
    mtx[1][1] = 1.0f / mtx[1][1];
    mtx[2][2] = 1.0f / mtx[2][2];
    mtx[0][3] = 0.0f;
    mtx[1][3] = 0.0f;
    mtx[2][3] = 0.0f;

    if (!sNBTFlag) {
        J3DNativeShape::setNormal(0, mtx);
    } else {
        Vec* nbt_scale = j3dSys.getNBTScale();
        J3DScaleNrmMtx(mtx, *nbt_scale);
        J3DNativeShape::setNormal(0, mtx);
    }
}

void J3DShapeMtxYBBoardConcatView::load() const {
    Mtx mtx1;
    Mtx33 mtx2;
    u16 draw_mtx_index = j3dSys.getModel()->getModelData()->getDrawMtxIndex(mUseMtxIndex);

    if (j3dSys.getModel()->getModelData()->getDrawMtxFlag(mUseMtxIndex) == 0) {
        NebulaMtxConcat(j3dSys.getViewMtx(), j3dSys.getModel()->getMtxBuffer()->getUserAnmMtx(draw_mtx_index), mtx1);
    } else {
        NebulaMtxConcat(j3dSys.getViewMtx(), j3dSys.getModel()->getWeightAnmMtx(draw_mtx_index), mtx1);
    }

    J3DCalcYBBoardMtx(mtx1);
    J3DNativeShape::setPosition(0, mtx1);

    if (sCurrentScaleFlag[mUseMtxIndex] == 1) {
        if (!sNBTFlag) {
            J3DNativeShape::setNormal(0, mtx1);
        } else {
            Vec* nbt_scale = j3dSys.getNBTScale();
            J3DScaleNrmMtx(mtx1, *nbt_scale);
            J3DNativeShape::setNormal(0, mtx1);
        }
    } else {
        J3DPSCalcInverseTranspose(mtx1, mtx2);
        if (!sNBTFlag) {
            J3DNativeShape::setNormal33(0, mtx2);
        } else {
            Vec* nbt_scale = j3dSys.getNBTScale();
            J3DScaleNrmMtx33(mtx2, *nbt_scale);
            J3DNativeShape::setNormal33(0, mtx2);
        }
    }
}

u8* J3DShapeMtx::sCurrentScaleFlag;
bool J3DShapeMtx::sNBTFlag;
u32 J3DShapeMtx::sTexMtxLoadType;
J3DTexGenBlock* J3DDifferedTexMtx::sTexGenBlock;
J3DTexMtxObj* J3DDifferedTexMtx::sTexMtxObj;

void J3DPSMtx33Copy(Mtx3P src, Mtx3P dst) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            dst[i][j] = src[i][j];
        }
    }
}

void J3DPSMtx33CopyFrom34(MtxPtr src, Mtx3P dst) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            dst[i][j] = src[i][j];
        }
    }
}
