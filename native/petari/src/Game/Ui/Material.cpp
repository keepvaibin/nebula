#include "Game/Ui/Animation.hpp"
#include "Game/Render/Material.hpp"
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <string.h>
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Material.hpp"
#include "Game/Ui/ResourceAccessor.hpp"
#include "Game/Ui/TexMap.hpp"
#include "Game/Ui/UiTypes.hpp"
#include <nebula_geometry.hpp>
#include "Game/Ui/UiInlines.hpp"
#include <cstdio>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_CalcTextureMtx_f1, "CalcTextureMtx.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_CalcTextureMtx_f2, "CalcTextureMtx.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f1, "Setup.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f2, "Setup.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f3, "Setup.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f4, "Setup.f4", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f5, "Setup.f5", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f6, "Setup.f6", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f7, "Setup.f7", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f8, "Setup.f8", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f9, "Setup.f9", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f10, "Setup.f10", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f11, "Setup.f11", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f12, "Setup.f12", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f13, "Setup.f13", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_Setup_f14, "Setup.f14", 255.0f);

const NebulaColor16 DefaultBlackColor = {0, 0, 0, 0};

namespace Ui {
    namespace {
        // The values a form's rows read; the lists are sized by the material's counted tables.
        struct LayoutValues {
            f32 registers[3][4];
            f32 constants[4][4];
            f32 material_color[4];
            f32 alpha_ref[2];
            u32 coord_count;
            f32* coord;  // coord_count x 12: each coordinate's matrix rows
            u32 scale_count;
            f32* map_size;    // scale_count x 2
            f32* coord_size;  // scale_count x 2
            u32 warp_count;
            f32* warp;  // warp_count x 6: each indirect matrix's two rows
        };

        // `count` zeroed entries of this frame's memory; none for none.
        template < class T >
        T* frameZeroed(u32 count) {
            if (count == 0) {
                return nullptr;
            }
            T* const pItems = Frame::alloc< T >(count);
            memset(static_cast< void* >(pItems), 0, count * sizeof(T));
            return pItems;
        }

        void layoutRow(u32 row, const LayoutValues& rValues, f32* pOut) {
            pOut[0] = pOut[1] = pOut[2] = pOut[3] = 0.0f;
            if (row >= NEBULA_ROW_COORD) {
                const u32 index = row - NEBULA_ROW_COORD;
                if (index / 3 < rValues.coord_count) {
                    memcpy(pOut, rValues.coord + (index / 3) * 12 + (index % 3) * 4, 4 * sizeof(f32));
                } else {
                    pOut[index % 3] = 1.0f;  // a coordinate the material does not name: the identity
                }
            } else if (row >= NEBULA_ROW_WARP) {
                const u32 index = row - NEBULA_ROW_WARP;
                if (index / 2 < rValues.warp_count) {
                    memcpy(pOut, rValues.warp + (index / 2) * 6 + (index % 2) * 3, 3 * sizeof(f32));
                }
            } else if (row >= NEBULA_ROW_SCALE) {
                const u32 index = row - NEBULA_ROW_SCALE;
                if (index < rValues.scale_count) {
                    pOut[0] = rValues.coord_size[index * 2];
                    pOut[1] = rValues.coord_size[index * 2 + 1];
                    pOut[2] = rValues.map_size[index * 2];
                    pOut[3] = rValues.map_size[index * 2 + 1];
                } else {
                    pOut[0] = pOut[1] = pOut[2] = pOut[3] = 1.0f;
                }
            } else if (row == NEBULA_ROW_ALPHA_REF) {
                pOut[0] = rValues.alpha_ref[0];
                pOut[1] = rValues.alpha_ref[1];
            } else if (row == NEBULA_ROW_MATERIAL_COLOR) {
                memcpy(pOut, rValues.material_color, sizeof(rValues.material_color));
            } else if (row >= NEBULA_ROW_CONSTANT && row < NEBULA_ROW_MATERIAL_COLOR) {
                memcpy(pOut, rValues.constants[row - NEBULA_ROW_CONSTANT], 4 * sizeof(f32));
            } else if (row < NEBULA_ROW_CONSTANT) {
                memcpy(pOut, rValues.registers[row], 4 * sizeof(f32));
            }
            // The second material colour, ambient, fog and the replacing forms' values: none in a layout.
        }

        bool IsWhiteColor(const NebulaColor16& color) {
            return color.r == 255 && color.g == 255 && color.b == 255 && color.a == 255;
        }

        bool operator==(const NebulaColor16& rLhs, const NebulaColor16& rRhs) {
            return rLhs.r == rRhs.r && rLhs.g == rRhs.g && rLhs.b == rRhs.b && rLhs.a == rRhs.a;
        }

        bool operator!=(const NebulaColor16& rLhs, const NebulaColor16& rRhs) {
            return !(rLhs == rRhs);
        }

        void CalcTextureMtx(Mtx34f* pMtx, const TexSRT& rSrt) {
            Vec2f center(cNum_CalcTextureMtx_f1, cNum_CalcTextureMtx_f2);

            f32 cr = detail::CosDeg(rSrt.rotate);
            f32 sr = detail::SinDeg(rSrt.rotate);

            f32 a0, a1;

            a0 = cr * rSrt.scale.x;
            a1 = -sr * rSrt.scale.y;

            pMtx->mMtx[0][0] = a0;
            pMtx->mMtx[0][1] = a1;
            pMtx->mMtx[0][2] = 0.0f;
            pMtx->mMtx[0][3] = rSrt.translate.x + center.x + a0 * -center.x + a1 * -center.y;

            a0 = sr * rSrt.scale.x;
            a1 = cr * rSrt.scale.y;

            pMtx->mMtx[1][0] = a0;
            pMtx->mMtx[1][1] = a1;
            pMtx->mMtx[1][2] = 0.0f;
            pMtx->mMtx[1][3] = rSrt.translate.y + center.y + a0 * -center.x + a1 * -center.y;

            pMtx->mMtx[2][0] = 0.0f;
            pMtx->mMtx[2][1] = 0.0f;
            pMtx->mMtx[2][2] = 1.0f;
            pMtx->mMtx[2][3] = 0.0f;
        }

        void CalcIndTexMtx(f32 mtx[2][3], const TexSRT& rSrt) {
            f32 cr = detail::CosDeg(rSrt.rotate);
            f32 sr = detail::SinDeg(rSrt.rotate);

            mtx[0][0] = cr * rSrt.scale.x;
            mtx[0][1] = -sr * rSrt.scale.y;
            mtx[0][2] = rSrt.translate.x;

            mtx[1][0] = sr * rSrt.scale.x;
            mtx[1][1] = cr * rSrt.scale.y;
            mtx[1][2] = rSrt.translate.y;
        }

        inline u32 GetTexMtxIdx(u32 mtx) {
            return (mtx - MODEL_FILE_MATRIX_FIRST) / 3;
        }

        void SetColorComponentValue(Color8* pColor, u32 idx, s16 value) {
            u8 bvalue = Min< s16 >(Max< s16 >(value, 0), 255);

            switch (idx % 4) {
            case 0: {
                pColor->r = bvalue;
                break;
            }

            case 1: {
                pColor->g = bvalue;
                break;
            }

            case 2: {
                pColor->b = bvalue;
                break;
            }

            case 3: {
                pColor->a = bvalue;
                break;
            }

            default: {
                break;
            }
            }
        }

        u32 CalcOffsetTexSRTAry(const detail::StorageNums& rNums) {
            return rNums.texMap * ROUND_UP(sizeof(TexMap), 4);
        }

        u32 CalcOffsetTexCoordGenAry(const detail::StorageNums& rNums) {
            return rNums.texSRT * ROUND_UP(sizeof(TexSRT), 4) + CalcOffsetTexSRTAry(rNums);
        }

        u32 CalcOffsetChanCtrlAry(const detail::StorageNums& rNums) {
            return rNums.texCoordGen * ROUND_UP(sizeof(TexCoordGen), 4) + CalcOffsetTexCoordGenAry(rNums);
        }

        u32 CalcOffsetMatColAry(const detail::StorageNums& rNums) {
            return rNums.chanCtrl * ROUND_UP(sizeof(ChanCtrl), 4) + CalcOffsetChanCtrlAry(rNums);
        }

        u32 CalcOffsetGetAlphaCompare(const detail::StorageNums& rNums) {
            return rNums.matCol * ROUND_UP(sizeof(Color8), 4) + CalcOffsetMatColAry(rNums);
        }

        u32 CalcOffsetBlendMode(const detail::StorageNums& rNums) {
            return rNums.alpComp * ROUND_UP(sizeof(AlphaCompare), 4) + CalcOffsetGetAlphaCompare(rNums);
        }

        u32 CalcOffsetIndTexSRTAry(const detail::StorageNums& rNums) {
            return rNums.blendMode * ROUND_UP(sizeof(BlendMode), 4) + CalcOffsetBlendMode(rNums);
        }

        void InitTexSRT(TexSRT* texSRTs, u32 num) {
            for (u32 i = 0; i < num; ++i) {
                texSRTs[i].translate = Vec2f(0.f, 0.f);
                texSRTs[i].rotate = 0.f;
                texSRTs[i].scale = Vec2f(1.f, 1.f);
            }
        }

    };  // namespace

    Material::Material() {
        Init();
    }


    namespace {
        TexSRT RecordSRT(const UiSrtData& rSrt) {
            TexSRT out;
            out.translate = Vec2f(rSrt.mTranslate[0], rSrt.mTranslate[1]);
            out.rotate = rSrt.mRotate;
            out.scale = Vec2f(rSrt.mScale[0], rSrt.mScale[1]);
            return out;
        }
    }  // namespace

    // A cooked material (UiMaterialData): room for each counted list, the forms written by the cook.
    Material::Material(const UiMaterialData* pRes, const ResBlockSet& resBlockSet) {
        Init();

        SetName(resBlockSet.String(pRes->mName));

        for (int i = 0; i < 3; ++i) {
            mRegisterColors[i].r = pRes->mRegisters[i][0];
            mRegisterColors[i].g = pRes->mRegisters[i][1];
            mRegisterColors[i].b = pRes->mRegisters[i][2];
            mRegisterColors[i].a = pRes->mRegisters[i][3];
        }

        for (int i = 0; i < 4; ++i) {
            mConstantColors[i] = detail::RecordColor(pRes->mConstants[i]);
        }

        const u32 texMapNum = pRes->mMaps.mCount;
        const u32 texSRTNum = pRes->mSrts.mCount;
        const u32 texCoordGenNum = pRes->mGenerators.mCount;
        const bool allocChanCtrl = (pRes->mFlags & UiMaterialFlag_Channel) != 0;
        const bool allocMatCol = (pRes->mFlags & UiMaterialFlag_MaterialColor) != 0;
        const bool allocAlpComp = (pRes->mFlags & UiMaterialFlag_AlphaCompare) != 0;
        const bool allocBlendMode = (pRes->mFlags & UiMaterialFlag_Blend) != 0;
        const u32 indTexSRTNum = pRes->mIndirectSrts.mCount;

        ReserveStorage(texMapNum, texSRTNum, texCoordGenNum, indTexSRTNum, allocChanCtrl, allocMatCol,
                     allocAlpComp, allocBlendMode);

        if (mpStorage) {
            SetTextureNum(texMapNum);
            const UiMapData* const pMaps = resBlockSet.At< UiMapData >(pRes->mMaps.mOffset);
            TexMap* const texMaps = GetTexMapAry();
            for (u32 i = 0; i < texMapNum; ++i) {
                const UiMapData& rMap = pMaps[i];
                void* const pTplRes = resBlockSet.pResAccessor->GetResource(0x74696D67, resBlockSet.TextureName(rMap.mTexture), nullptr);
                texMaps[i].ReplaceImage(pTplRes, 0);
                texMaps[i].SetWrapMode(rMap.mWrapS, rMap.mWrapT);
                texMaps[i].SetFilter(rMap.mMinFilter, rMap.mMagFilter);
            }

            const UiSrtData* const pSrts = resBlockSet.At< UiSrtData >(pRes->mSrts.mOffset);
            TexSRT* texSRTs = GetTexSRTAry();
            for (u32 i = 0; i < texSRTNum; ++i) {
                texSRTs[i] = RecordSRT(pSrts[i]);
            }

            const UiGeneratorData* const pGenerators = resBlockSet.At< UiGeneratorData >(pRes->mGenerators.mOffset);
            TexCoordGen* texCoordGens = GetTexCoordGenAry();
            SetTexCoordGenNum(texCoordGenNum);
            for (u32 i = 0; i < mNum.texCoordGen; ++i) {
                texCoordGens[i] = TexCoordGen(pGenerators[i].mType, pGenerators[i].mSource, pGenerators[i].mMatrix);
            }

            if (allocChanCtrl) {
                GetChanCtrlAry()[0] = ChanCtrl(pRes->mChannel[0], pRes->mChannel[1]);
            }

            if (allocMatCol) {
                GetMatColAry()[0] = detail::RecordColor(pRes->mMaterialColor);
            }

            const UiSrtData* const pIndSrts = resBlockSet.At< UiSrtData >(pRes->mIndirectSrts.mOffset);
            TexSRT* const indTexSRTs = GetIndTexSRTAry();
            for (u32 i = 0; i < indTexSRTNum; ++i) {
                indTexSRTs[i] = RecordSRT(pIndSrts[i]);
            }

            if (allocAlpComp) {
                const u8* const a = pRes->mAlphaCompare;
                *GetAlphaComparePtr() = AlphaCompare(a[0] & 15, a[2], a[1], a[0] >> 4, a[3]);
            }

            if (allocBlendMode) {
                const u8* const b = pRes->mBlend;
                *GetBlendModePtr() = BlendMode(b[0], b[1], b[2], b[3]);
            }
        }
        mpFile = resBlockSet.pLayout;
        mNativeForms = pRes->mForms;
        mCoordMap = resBlockSet.At< u8 >(pRes->mCoordMap.mOffset);
        mCoordCount = pRes->mCoordMap.mCount;
        mHasStages = (pRes->mFlags & UiMaterialFlag_Stages) != 0;
    }


    void Material::Init() {
        mRegisterColors[0] = DefaultBlackColor;
        mRegisterColors[1].a = mRegisterColors[1].b = mRegisterColors[1].g = mRegisterColors[1].r = 255;
        mRegisterColors[2].a = mRegisterColors[2].b = mRegisterColors[2].g = mRegisterColors[2].r = 255;

        InitStorageNums(&mCap);
        InitStorageNums(&mNum);

        mpStorage = 0;
        mpFile = nullptr;
        mNativeForms = nullptr;
        mCoordMap = nullptr;
        mCoordCount = 0;
        mHasStages = false;
    }

    Material::~Material() {
        UnbindAllAnimation();

        if (mpStorage != NULL) {
            Layout::FreeMemory(mpStorage);
            mpStorage = NULL;
        }
    }

    void Material::InitStorageNums(detail::StorageNums* ptr) {
        ptr->texMap = 0;
        ptr->texSRT = 0;
        ptr->texCoordGen = 0;
        ptr->indSRT = 0;
        ptr->chanCtrl = 0;
        ptr->matCol = 0;
        ptr->alpComp = 0;
        ptr->blendMode = 0;
    }

    void Material::ReserveStorage(u32 texMapNum, u32 texSRTNum, u32 texCoordGenNum, u32 indSRTNum,
                                bool allocChanCtrl, bool allocMatCol, bool allocAlpComp, bool allocBlendMode) {
        const u32 chanCtrlNum = allocChanCtrl ? 1 : 0;
        const u32 matColNum = allocMatCol ? 1 : 0;
        const u32 alpCompNum = allocAlpComp ? 1 : 0;
        const u32 blendModeNum = allocBlendMode ? 1 : 0;

        if (mCap.texMap < texMapNum || mCap.texSRT < texSRTNum || mCap.texCoordGen < texCoordGenNum ||
            mCap.indSRT < indSRTNum || mCap.chanCtrl < chanCtrlNum || mCap.matCol < matColNum || mCap.alpComp < alpCompNum ||
            mCap.blendMode < blendModeNum) {
            if (mpStorage) {
                Layout::FreeMemory(mpStorage);
                mpStorage = 0;

                InitStorageNums(&mCap);
                InitStorageNums(&mNum);
            }

            mpStorage = Layout::AllocMemory(sizeof(TexMap) * texMapNum + sizeof(TexSRT) * texSRTNum +
                                          4 * (texCoordGenNum + chanCtrlNum + matColNum + alpCompNum + blendModeNum) +
                                          sizeof(TexSRT) * indSRTNum);
            if (mpStorage) {
                mCap.texMap = texMapNum;
                mCap.texSRT = texSRTNum;
                mCap.texCoordGen = texCoordGenNum;
                mCap.indSRT = indSRTNum;
                mCap.chanCtrl = chanCtrlNum;
                mCap.matCol = matColNum;
                mCap.alpComp = alpCompNum;
                mCap.blendMode = blendModeNum;

                mNum.texSRT = mCap.texSRT;
                InitTexSRT(GetTexSRTAry(), mNum.texSRT);
                mNum.indSRT = mCap.indSRT;
                InitTexSRT(GetIndTexSRTAry(), mNum.indSRT);

                mNum.chanCtrl = mCap.chanCtrl;
                if (mNum.chanCtrl) {
                    GetChanCtrlAry()[0] = ChanCtrl();
                }

                mNum.matCol = mCap.matCol;
                if (mNum.matCol) {
                    GetMatColAry()[0] = 0xFFFFFFFFu;
                }

                mNum.alpComp = mCap.alpComp;
                if (mNum.alpComp) {
                    *GetAlphaComparePtr() = AlphaCompare();
                }

                mNum.blendMode = mCap.blendMode;
                if (mNum.blendMode) {
                    *GetBlendModePtr() = BlendMode();
                }
            }
        }
    }

    const TexMap* Material::GetTexMapAry() const {
        return detail::ConvertOffsToPtr< TexMap >(mpStorage, 0);
    }

    TexMap* Material::GetTexMapAry() {
        return detail::ConvertOffsToPtr< TexMap >(mpStorage, 0);
    }

    TexSRT* Material::GetTexSRTAry() {
        return detail::ConvertOffsToPtr< TexSRT >(mpStorage, CalcOffsetTexSRTAry(mCap));
    }

    TexCoordGen* Material::GetTexCoordGenAry() {
        return detail::ConvertOffsToPtr< TexCoordGen >(mpStorage, CalcOffsetTexCoordGenAry(mCap));
    }

    ChanCtrl* Material::GetChanCtrlAry() {
        return detail::ConvertOffsToPtr< ChanCtrl >(mpStorage, CalcOffsetChanCtrlAry(mCap));
    }

    Color8* Material::GetMatColAry() {
        return detail::ConvertOffsToPtr< Color8 >(mpStorage, CalcOffsetMatColAry(mCap));
    }

    AlphaCompare* Material::GetAlphaComparePtr() {
        return detail::ConvertOffsToPtr< AlphaCompare >(mpStorage, CalcOffsetGetAlphaCompare(mCap));
    }

    BlendMode* Material::GetBlendModePtr() {
        return detail::ConvertOffsToPtr< BlendMode >(mpStorage, CalcOffsetBlendMode(mCap));
    }

    TexSRT* Material::GetIndTexSRTAry() {
        return detail::ConvertOffsToPtr< TexSRT >(mpStorage, CalcOffsetIndTexSRTAry(mCap));
    }

    void Material::SetName(const char* pName) {
        mName.set(pName);
    }

    void Material::SetTextureNum(u32 num) {
        if (num > 0) {
            TexMap* const pTexMap = GetTexMapAry();

            for (u32 i = mNum.texMap; i < num; i++) {
                new (&pTexMap[i]) TexMap();
            }
        }

        mNum.texMap = num;
    }

    void Material::SetTexCoordGenNum(u32 num) {
        if (num > 0) {
            TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

            for (u32 i = mNum.texCoordGen; i < num; i++) {
                new (&pTexCoordGen[i]) TexCoordGen();
            }
        }

        mNum.texCoordGen = num;
    }

    void Material::SetColorElement(u32 idx, s16 value) {
        switch (idx) {
        case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_R:
        case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_G:
        case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_B:
        case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_A:
            if (mNum.matCol >= 1) {
                Color8* matCols = GetMatColAry();
                SetColorComponentValue(&matCols[0], (idx - detail::ANIMTARGET_MATCOLOR_MATCOLOR0_R) % 4, value);
            }
            break;

        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_R:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_G:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_B:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_A:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR1_R:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR1_G:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR1_B:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR1_A:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR2_R:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR2_G:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR2_B:
        case detail::ANIMTARGET_MATCOLOR_TEVCOLOR2_A: {
            const u32 regIdx = (idx - detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_R) / 4;
            switch ((idx - detail::ANIMTARGET_MATCOLOR_TEVCOLOR0_R) % 4) {
            case 0:
                mRegisterColors[regIdx].r = value;
                break;
            case 1:
                mRegisterColors[regIdx].g = value;
                break;
            case 2:
                mRegisterColors[regIdx].b = value;
                break;
            case 3:
                mRegisterColors[regIdx].a = value;
                break;
            }
        } break;

        case detail::ANIMTARGET_MATCOLOR_TEVKONST0_R:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST0_G:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST0_B:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST0_A:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST1_R:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST1_G:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST1_B:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST1_A:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST2_R:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST2_G:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST2_B:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST2_A:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST3_R:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST3_G:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST3_B:
        case detail::ANIMTARGET_MATCOLOR_TEVKONST3_A: {
            const u32 regIdx = (idx - detail::ANIMTARGET_MATCOLOR_TEVKONST0_R) / 4;
            SetColorComponentValue(&mConstantColors[regIdx], (idx - detail::ANIMTARGET_MATCOLOR_TEVKONST0_R) % 4, value);
        } break;
        }
    }

    bool Material::Setup(bool modulate, u8 alpha) {
        // A picture made in code has no material in a layout file, so no form from the cook: a written shader.
        if (mNativeForms == nullptr) {
            if (mNum.texMap == 0) {
                detail::SetDrawNothing();
                return false;
            }
            SiteTexture texObj;
            GetTexMapAry()[0].Get(&texObj);
            SiteMaterial material;
            material.init(NEBULA_SHADER_J2D_PICTURE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
            material.setColor(12, 255, 255, 255, 255);
            material.constants[16] = 1.0f;
            material.constants[17] = 1.0f;
            material.setTexture(0, texObj);
            material.mTextures[0].mip_filter = 0;
            material.mTextures[0].anisotropy = 0;
            material.mTextures[0].lod_bias = 0.0f;
            material.mTextures[0].min_lod = 0.0f;
            material.mTextures[0].max_lod = 0.0f;
            material.mTextures[0].mag_linear = 1;
            material.mTextures[0].min_linear = 1;
            detail::SetDrawPicture(material);
            return true;
        }

        LayoutValues values;
        memset(&values, 0, sizeof(values));
        bool useVtxColor = true;
        bool useMatColor = false;

        // Channel 0 unlit: its colour and alpha from the material colour or the vertex.
        if (IsChanCtrlCap()) {
            const ChanCtrl* const pChanCtrl = GetChanCtrlAry();
            useVtxColor = pChanCtrl->GetColorSrc() == MODEL_FILE_SOURCE_VERTEX || pChanCtrl->GetAlphaSrc() == MODEL_FILE_SOURCE_VERTEX;
            useMatColor = pChanCtrl->GetColorSrc() == MODEL_FILE_SOURCE_REGISTER || pChanCtrl->GetAlphaSrc() == MODEL_FILE_SOURCE_REGISTER;
        }

        bool useRasStage = false;

        if (useVtxColor) {
            useRasStage = useRasStage || modulate;
        }

        if (useMatColor) {
            Color8 matColor = 0xFFFFFFFFu;

            if (IsMatColorCap()) {
                matColor = *GetMatColAry();
            }

            matColor = detail::MultipleAlpha(matColor, alpha);
            values.material_color[0] = matColor.r / cNum_Setup_f1;
            values.material_color[1] = matColor.g / cNum_Setup_f2;
            values.material_color[2] = matColor.b / cNum_Setup_f3;
            values.material_color[3] = matColor.a / cNum_Setup_f4;

            useRasStage = useRasStage || matColor != 0xFFFFFFFFu;
        } else {
            values.material_color[0] = values.material_color[1] = values.material_color[2] = values.material_color[3] = 1.0f;
        }

        // The material's counted tables size what the rows read.
        const u32 mapCount = mNum.texMap;
        const u32 genCount = mNum.texCoordGen;
        const u32 coordCount = mCoordCount > genCount ? mCoordCount : genCount;
        const u32 scaleCount = coordCount > mapCount ? coordCount : mapCount;
        values.coord_count = coordCount;
        values.coord = frameZeroed< f32 >(coordCount * 12);
        values.scale_count = scaleCount;
        values.map_size = frameZeroed< f32 >(scaleCount * 2);
        values.coord_size = frameZeroed< f32 >(scaleCount * 2);
        values.warp_count = mNum.indSRT;
        values.warp = frameZeroed< f32 >(values.warp_count * 6);

        bool setTexMtx = false;
        bool* const useTexMtx = frameZeroed< bool >(mNum.texSRT);
        u8* const places = frameZeroed< u8 >(coordCount);
        for (u32 c = 0; c < coordCount; c++) {
            places[c] = MODEL_FILE_MATRIX_IDENTITY;
            values.coord[c * 12] = values.coord[c * 12 + 5] = values.coord[c * 12 + 10] = 1.0f;
        }
        if (mNum.texCoordGen > 0) {
            const TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

            for (u32 i = 0; i < genCount; i++) {
                u32 texMtx = pTexCoordGen[i].GetTexMtx();

                if (pTexCoordGen[i].GetTexGenType() == MODEL_FILE_TEXGEN_MATRIX_2x4 && texMtx != MODEL_FILE_MATRIX_IDENTITY) {
                    if (GetTexMtxIdx(texMtx) < mNum.texSRT) {
                        useTexMtx[GetTexMtxIdx(texMtx)] = true;
                    }
                    setTexMtx = true;
                }

                places[i] = static_cast< u8 >(texMtx);
            }
        }

        if (setTexMtx) {
            const TexSRT* const pTexSrt = GetTexSRTAry();

            for (u32 c = 0; c < coordCount; c++) {
                if (places[c] == MODEL_FILE_MATRIX_IDENTITY || GetTexMtxIdx(places[c]) >= mNum.texSRT || !useTexMtx[GetTexMtxIdx(places[c])]) {
                    continue;
                }

                Mtx34f mtx;
                CalcTextureMtx(&mtx, pTexSrt[GetTexMtxIdx(places[c])]);
                memcpy(values.coord + c * 12, &mtx, 12 * sizeof(f32));
            }
        }

        SiteTexture* const maps = frameZeroed< SiteTexture >(mapCount);
        bool* const mapUsed = frameZeroed< bool >(mapCount);
        if (mNum.texMap > 0) {
            const TexMap* const pTexMap = GetTexMapAry();

            for (u32 i = 0; i < mapCount; i++) {
                pTexMap[i].Get(&maps[i]);
                mapUsed[i] = maps[i].mImage != nullptr;
            }
        }

        for (u32 r = 0; r < 3; r++) {
            values.registers[r][0] = mRegisterColors[r].r / cNum_Setup_f5;
            values.registers[r][1] = mRegisterColors[r].g / cNum_Setup_f6;
            values.registers[r][2] = mRegisterColors[r].b / cNum_Setup_f7;
            values.registers[r][3] = mRegisterColors[r].a / cNum_Setup_f8;
        }
        for (u32 k = 0; k < 4; k++) {
            values.constants[k][0] = mConstantColors[k].r / cNum_Setup_f9;
            values.constants[k][1] = mConstantColors[k].g / cNum_Setup_f10;
            values.constants[k][2] = mConstantColors[k].b / cNum_Setup_f11;
            values.constants[k][3] = mConstantColors[k].a / cNum_Setup_f12;
        }

        // Each map's size, and each coordinate's scale: the size of the map the cook found it samples.
        for (u32 k = 0; k < scaleCount; k++) {
            const bool own = k < mapCount && mapUsed[k];
            values.map_size[k * 2] = own && maps[k].mWidth != 0 ? maps[k].mWidth : 1.0f;
            values.map_size[k * 2 + 1] = own && maps[k].mHeight != 0 ? maps[k].mHeight : 1.0f;
            const u32 map = k < mCoordCount ? mCoordMap[k] : 255u;
            const bool known = map < mapCount && mapUsed[map];
            values.coord_size[k * 2] = known && maps[map].mWidth != 0 ? maps[map].mWidth : values.map_size[k * 2];
            values.coord_size[k * 2 + 1] = known && maps[map].mHeight != 0 ? maps[map].mHeight : values.map_size[k * 2 + 1];
        }
        if (mHasStages || mNum.texMap == 0) {
            useRasStage = true;
        }

        {
            const TexSRT* const pIndTexSrt = GetIndTexSRTAry();

            for (u32 i = 0; i < values.warp_count; i++) {
                CalcIndTexMtx(reinterpret_cast< f32 (*)[3] >(values.warp + i * 6), pIndTexSrt[i]);
            }
        }

        if (IsAlphaCompareCap()) {
            const AlphaCompare* const pAlphaComp = GetAlphaComparePtr();
            values.alpha_ref[0] = pAlphaComp->GetRef0() / cNum_Setup_f13;
            values.alpha_ref[1] = pAlphaComp->GetRef1() / cNum_Setup_f14;
        }

        PassState state = PassStates::plain();
        state.depth_func = NEBULA_COMPARE_NEVER;
        if (IsBlendModeCap()) {
            const BlendMode* const pBlendMode = GetBlendModePtr();
            const u8 type = pBlendMode->GetType();
            // NEBULA_FACTOR_*: source colour factors 2 and 3 are the destination's colour.
            const u8 src = pBlendMode->GetSrcFactor();
            state.blend_op = type == MODEL_FILE_BLEND_ADD ? NEBULA_BLEND_ADD : type == MODEL_FILE_BLEND_SUBTRACT ? NEBULA_BLEND_SUBTRACT : NEBULA_BLEND_REPLACE;
            state.blend_src = src == MODEL_FILE_FACTOR_DST_COLOR ? NEBULA_FACTOR_DST_COLOR : src == MODEL_FILE_FACTOR_INV_DST_COLOR ? NEBULA_FACTOR_INV_DST_COLOR : src;
            state.blend_dst = pBlendMode->GetDstFactor();
        } else {
            state.blend_op = NEBULA_BLEND_ADD;
            state.blend_src = NEBULA_FACTOR_SRC_ALPHA;
            state.blend_dst = NEBULA_FACTOR_INV_SRC_ALPHA;
        }

        const bool remap = mRegisterColors[0] != DefaultBlackColor || !IsWhiteColor(mRegisterColors[1]);
        const UiFormData* pForm = mNativeForms + ((useRasStage ? 1 : 0) | (remap ? 2 : 0));
        const u32* const pFormRows = uiAt< u32 >(mpFile, pForm->mRows.mOffset);
        const u8* const pFormTextures = uiAt< u8 >(mpFile, pForm->mTextures.mOffset);
        const u8* const pFormInputs = uiAt< u8 >(mpFile, pForm->mInputs.mOffset);

        const u32 vertexRows = pForm->mVertexRows;
        const u32 rowCount = pForm->mRows.mCount;
        f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(rowCount * 16, 16));
        DrawMatrixRows coordRows;
        for (u32 i = 0; i < rowCount; i++) {
            const u32 row = pFormRows[i];
            layoutRow(row, values, pRows[i]);
            if (row >= NEBULA_ROW_COORD && i < vertexRows) {
                if ((row - NEBULA_ROW_COORD) % 3 == 0) { coordRows.add(i); }
            }
        }
        const u32 constants = Frame::addConstants(NEBULA_NONE32, vertexRows, rowCount - vertexRows, pRows, coordRows.data(), coordRows.count());

        // The renderer's draw tables and pipeline layouts are counted: any number of textures and inputs.
        const u32 textureCount = pForm->mTextures.mCount;
        NebulaFrameTexture* const list = frameZeroed< NebulaFrameTexture >(textureCount);
        for (u32 i = 0; i < textureCount; i++) {
            const u32 map = pFormTextures[i];
            if (map < mapCount && mapUsed[map]) {
                SiteTextures::toFrame(maps[map], &list[i]);
            }
        }
        const NebulaHostServices* pHost = nebula_host();
        const u32 table = textureCount != 0 ? pHost->texture_table(pHost->user, list, textureCount) : 0;

        // A layout material is the last word on ambient: models after it that keep the ambient in force read black.
        const NebulaColor8 black = {0, 0, 0, 0};
        MaterialAmbient::set(0, black);
        MaterialAmbient::set(1, black);

        detail::SetDrawForm(pForm, pFormInputs, constants, table, state.word());

        return useRasStage && useVtxColor;
    }

    void Material::BindAnimation(AnimTransform* pAnimTrans) {
        pAnimTrans->Bind(this);
    }

    void Material::UnbindAnimation(AnimTransform* pAnimTrans) {
        detail::UnbindAnimationLink(&mAnimList, pAnimTrans);
    }

    void Material::UnbindAllAnimation() {
        UnbindAnimation(nullptr);
    }

    void Material::Animate() {
        for (AnimationLink* pLink : mAnimList) {
            if (pLink->IsEnable()) {
                AnimTransform* animTrans = pLink->GetAnimTransform();
                animTrans->Animate(pLink->GetIndex(), this);
            }
        }
    }

    void Material::AddAnimationLink(AnimationLink* pAnimLink) {
        mAnimList.PushBack(pAnimLink);
    }

    AnimationLink* Material::FindAnimationLink(AnimTransform* pAnimTrans) {
        return detail::FindAnimationLink(&mAnimList, pAnimTrans);
    }

    void Material::SetAnimationEnable(AnimTransform* pAnimTrans, bool enable) {
        AnimationLink* pAnimLink = FindAnimationLink(pAnimTrans);

        if (pAnimLink != NULL) {
            pAnimLink->SetEnable(enable);
        }
    }

    namespace detail {
        const Size GetTextureSize(Material* pMaterial, u32 idx) {
            if (idx >= pMaterial->GetTextureNum()) {
                return Size(0.0f, 0.0f);
            }

            const TexMap& rTexMap = pMaterial->GetTexture(idx);
            return rTexMap.GetSize();
        }
    };  // namespace detail
}  // namespace Ui
