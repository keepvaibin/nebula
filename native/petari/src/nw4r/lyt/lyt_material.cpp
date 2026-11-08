#include "nw4r/lyt/animation.h"
#include "Game/Render/Material.hpp"
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <string.h>
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "nw4r/lyt/layout.h"
#include "nw4r/lyt/material.h"
#include "nw4r/lyt/resourceAccessor.h"
#include "nw4r/lyt/texMap.h"
#include "nw4r/lyt/types.h"
#include "nw4r/math/triangular.h"
#include "nw4r/math/types.h"
#include "nw4r/ut/inlines.h"
#include <cstdio>

const NebulaColor16 DefaultBlackColor = {0, 0, 0, 0};

namespace nw4r {
    namespace lyt {
        namespace {
            // The values a form's rows read.
            struct LayoutValues {
                f32 registers[3][4];
                f32 constants[4][4];
                f32 material_color[4];
                f32 warp[3][2][3];
                f32 coord[8][12];
                f32 map_size[8][2];
                f32 coord_size[8][2];
                f32 alpha_ref[2];
            };

            void layoutRow(u32 row, const LayoutValues& rValues, f32* pOut) {
                pOut[0] = pOut[1] = pOut[2] = pOut[3] = 0.0f;
                if (row >= NEBULA_ROW_COORD) {
                    const u32 index = row - NEBULA_ROW_COORD;
                    memcpy(pOut, rValues.coord[index / 3] + (index % 3) * 4, 4 * sizeof(f32));
                } else if (row >= NEBULA_ROW_WARP) {
                    const u32 index = row - NEBULA_ROW_WARP;
                    memcpy(pOut, rValues.warp[index / 2][index % 2], 3 * sizeof(f32));
                } else if (row >= NEBULA_ROW_SCALE) {
                    const u32 index = row - NEBULA_ROW_SCALE;
                    pOut[0] = rValues.coord_size[index][0];
                    pOut[1] = rValues.coord_size[index][1];
                    pOut[2] = rValues.map_size[index][0];
                    pOut[3] = rValues.map_size[index][1];
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

            void CalcTextureMtx(nw4r::math::MTX34* pMtx, const nw4r::lyt::TexSRT& rSrt) {
                nw4r::math::VEC2 center(0.5f, 0.5f);

                f32 cr = nw4r::math::CosDeg(rSrt.rotate);
                f32 sr = nw4r::math::SinDeg(rSrt.rotate);

                f32 a0, a1;

                a0 = cr * rSrt.scale.x;
                a1 = -sr * rSrt.scale.y;

                pMtx->_00 = a0;
                pMtx->_01 = a1;
                pMtx->_02 = 0.0f;
                pMtx->_03 = rSrt.translate.x + center.x + a0 * -center.x + a1 * -center.y;

                a0 = sr * rSrt.scale.x;
                a1 = cr * rSrt.scale.y;

                pMtx->_10 = a0;
                pMtx->_11 = a1;
                pMtx->_12 = 0.0f;
                pMtx->_13 = rSrt.translate.y + center.y + a0 * -center.x + a1 * -center.y;

                pMtx->_20 = 0.0f;
                pMtx->_21 = 0.0f;
                pMtx->_22 = 1.0f;
                pMtx->_23 = 0.0f;
            }

            void CalcIndTexMtx(f32 mtx[2][3], const nw4r::lyt::TexSRT& rSrt) {
                f32 cr = nw4r::math::CosDeg(rSrt.rotate);
                f32 sr = nw4r::math::SinDeg(rSrt.rotate);

                mtx[0][0] = cr * rSrt.scale.x;
                mtx[0][1] = -sr * rSrt.scale.y;
                mtx[0][2] = rSrt.translate.x;

                mtx[1][0] = sr * rSrt.scale.x;
                mtx[1][1] = cr * rSrt.scale.y;
                mtx[1][2] = rSrt.translate.y;
            }

            inline u32 GetTexMtxIdx(u32 mtx) {
                return (mtx - MODEL_FILE_TEXMTX0) / 3;
            }

            void SetColorComponentValue(nw4r::ut::Color* pColor, u32 idx, s16 value) {
                u8 bvalue = nw4r::ut::Min< s16 >(nw4r::ut::Max< s16 >(value, 0), 255);

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

            u32 CalcOffsetTexSRTAry(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.texMap * ROUND_UP(sizeof(nw4r::lyt::TexMap), 4);
            }

            u32 CalcOffsetTexCoordGenAry(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.texSRT * ROUND_UP(sizeof(nw4r::lyt::TexSRT), 4) + CalcOffsetTexSRTAry(rNums);
            }

            u32 CalcOffsetChanCtrlAry(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.texCoordGen * ROUND_UP(sizeof(nw4r::lyt::TexCoordGen), 4) + CalcOffsetTexCoordGenAry(rNums);
            }

            u32 CalcOffsetMatColAry(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.chanCtrl * ROUND_UP(sizeof(nw4r::lyt::ChanCtrl), 4) + CalcOffsetChanCtrlAry(rNums);
            }

            u32 CalcOffsetGetAlphaCompare(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.matCol * ROUND_UP(sizeof(nw4r::ut::Color), 4) + CalcOffsetMatColAry(rNums);
            }

            u32 CalcOffsetBlendMode(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.alpComp * ROUND_UP(sizeof(nw4r::lyt::AlphaCompare), 4) + CalcOffsetGetAlphaCompare(rNums);
            }

            u32 CalcOffsetIndTexSRTAry(const nw4r::lyt::detail::StorageNums& rNums) {
                return rNums.blendMode * ROUND_UP(sizeof(nw4r::lyt::BlendMode), 4) + CalcOffsetBlendMode(rNums);
            }

            void InitTexSRT(nw4r::lyt::TexSRT* texSRTs, u32 num) {
                for (u32 i = 0; i < num; ++i) {
                    texSRTs[i].translate = nw4r::math::VEC2(0.f, 0.f);
                    texSRTs[i].rotate = 0.f;
                    texSRTs[i].scale = nw4r::math::VEC2(1.f, 1.f);
                }
            }

        };  // namespace

        Material::Material() {
            Init();
            memset(mName, 0, sizeof(mName));
        }


        namespace {
            TexSRT RecordSRT(const f32 (&srt)[5]) {
                TexSRT out;
                out.translate = math::VEC2(srt[0], srt[1]);
                out.rotate = srt[2];
                out.scale = math::VEC2(srt[3], srt[4]);
                return out;
            }
        }  // namespace

        // A cooked material (UiMaterialData): counts within the storage caps, the forms written by the cook.
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

            const u8 texMapNum = pRes->mMapCount;
            const u8 texSRTNum = pRes->mSrtCount;
            const u8 texCoordGenNum = pRes->mGeneratorCount;
            const bool allocChanCtrl = pRes->mHasChannel != 0;
            const bool allocMatCol = pRes->mHasMaterialColor != 0;
            const bool allocAlpComp = pRes->mHasAlphaCompare != 0;
            const bool allocBlendMode = pRes->mHasBlend != 0;
            const u8 indTexSRTNum = pRes->mIndirectSrtCount;

            ReserveStorage(texMapNum, texSRTNum, texCoordGenNum, indTexSRTNum, allocChanCtrl, allocMatCol,
                         allocAlpComp, allocBlendMode);

            if (mpStorage) {
                SetTextureNum(texMapNum);
                TexMap* const texMaps = GetTexMapAry();
                for (u8 i = 0; i < texMapNum; ++i) {
                    const UiMapData& rMap = pRes->mMaps[i];
                    void* const pTplRes = resBlockSet.pResAccessor->GetResource(0x74696D67, resBlockSet.TextureName(rMap.mTexture), nullptr);
                    texMaps[i].ReplaceImage(pTplRes, 0);
                    texMaps[i].SetWrapMode(rMap.mWrapS, rMap.mWrapT);
                    texMaps[i].SetFilter(rMap.mMinFilter, rMap.mMagFilter);
                }

                TexSRT* texSRTs = GetTexSRTAry();
                for (int i = 0; i < texSRTNum; ++i) {
                    texSRTs[i] = RecordSRT(pRes->mSrts[i]);
                }

                TexCoordGen* texCoordGens = GetTexCoordGenAry();
                SetTexCoordGenNum(texCoordGenNum);
                for (int i = 0; i < mNum.texCoordGen; ++i) {
                    texCoordGens[i] = TexCoordGen(pRes->mGenerators[i][0], pRes->mGenerators[i][1], pRes->mGenerators[i][2]);
                }

                if (allocChanCtrl) {
                    GetChanCtrlAry()[0] = ChanCtrl(pRes->mChannel[0], pRes->mChannel[1]);
                }

                if (allocMatCol) {
                    GetMatColAry()[0] = detail::RecordColor(pRes->mMaterialColor);
                }

                TexSRT* const indTexSRTs = GetIndTexSRTAry();
                for (int i = 0; i < indTexSRTNum; ++i) {
                    indTexSRTs[i] = RecordSRT(pRes->mIndirectSrts[i]);
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
            mNativeForms = pRes->mForms;
            memcpy(mCoordMap, pRes->mCoordMap, sizeof(mCoordMap));
            mHasStages = pRes->mHasStages != 0;
        }


        void Material::Init() {
            mRegisterColors[0] = DefaultBlackColor;
            mRegisterColors[1].a = mRegisterColors[1].b = mRegisterColors[1].g = mRegisterColors[1].r = 255;
            mRegisterColors[2].a = mRegisterColors[2].b = mRegisterColors[2].g = mRegisterColors[2].r = 255;

            InitStorageNums(&mCap);
            InitStorageNums(&mNum);

            mpStorage = 0;
            mNativeForms = nullptr;
            // A material without its own stages: each texture by its own coordinate.
            for (u32 i = 0; i < 8; i++) {
                mCoordMap[i] = static_cast< u8 >(i);
            }
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

        void Material::ReserveStorage(u8 texMapNum, u8 texSRTNum, u8 texCoordGenNum, u8 indSRTNum,
                                    bool allocChanCtrl, bool allocMatCol, bool allocAlpComp, bool allocBlendMode) {
            const int chanCtrlNum = allocChanCtrl ? 1 : 0;
            const int matColNum = allocMatCol ? 1 : 0;
            const int alpCompNum = allocAlpComp ? 1 : 0;
            const int blendModeNum = allocBlendMode ? 1 : 0;

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
                        GetMatColAry()[0] = ut::Color::WHITE;
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

        ut::Color* Material::GetMatColAry() {
            return detail::ConvertOffsToPtr< ut::Color >(mpStorage, CalcOffsetMatColAry(mCap));
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
            strncpy(mName, pName, sizeof(mName) - 1);
            mName[sizeof(mName) - 1] = '\0';
        }

        void Material::SetTextureNum(u8 num) {
            if (num > 0) {
                TexMap* const pTexMap = GetTexMapAry();

                for (u32 i = mNum.texMap; i < num; i++) {
                    new (&pTexMap[i]) TexMap();
                }
            }

            mNum.texMap = num;
        }

        void Material::SetTexCoordGenNum(u8 num) {
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
                    ut::Color* matCols = GetMatColAry();
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
                useVtxColor = pChanCtrl->GetColorSrc() == MODEL_FILE_SRC_VTX || pChanCtrl->GetAlphaSrc() == MODEL_FILE_SRC_VTX;
                useMatColor = pChanCtrl->GetColorSrc() == MODEL_FILE_SRC_REG || pChanCtrl->GetAlphaSrc() == MODEL_FILE_SRC_REG;
            }

            bool useRasStage = false;

            if (useVtxColor) {
                useRasStage = useRasStage || modulate;
            }

            if (useMatColor) {
                ut::Color matColor = ut::Color::WHITE;

                if (IsMatColorCap()) {
                    matColor = *GetMatColAry();
                }

                matColor = detail::MultipleAlpha(matColor, alpha);
                values.material_color[0] = matColor.r / 255.0f;
                values.material_color[1] = matColor.g / 255.0f;
                values.material_color[2] = matColor.b / 255.0f;
                values.material_color[3] = matColor.a / 255.0f;

                useRasStage = useRasStage || matColor != ut::Color::WHITE;
            } else {
                values.material_color[0] = values.material_color[1] = values.material_color[2] = values.material_color[3] = 1.0f;
            }

            bool setTexMtx = false;
            bool useTexMtx[10];

            for (int i = 0; i < 10; i++) {
                useTexMtx[i] = false;
            }

            u8 places[8] = {MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY,
                            MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY, MODEL_FILE_IDENTITY};
            for (u32 c = 0; c < 8; c++) {
                values.coord[c][0] = values.coord[c][5] = values.coord[c][10] = 1.0f;
            }
            if (mNum.texCoordGen > 0) {
                const TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

                for (int i = 0; i < mNum.texCoordGen; i++) {
                    u32 texMtx = pTexCoordGen[i].GetTexMtx();

                    if (pTexCoordGen[i].GetTexGenType() == MODEL_FILE_TG_MTX2x4 && texMtx != MODEL_FILE_IDENTITY) {
                        useTexMtx[GetTexMtxIdx(texMtx)] = true;
                        setTexMtx = true;
                    }

                    if (i < 8) {
                        places[i] = static_cast< u8 >(texMtx);
                    }
                }
            }

            if (setTexMtx) {
                const TexSRT* const pTexSrt = GetTexSRTAry();

                for (u8 c = 0; c < 8; c++) {
                    if (places[c] == MODEL_FILE_IDENTITY || GetTexMtxIdx(places[c]) >= mNum.texSRT || !useTexMtx[GetTexMtxIdx(places[c])]) {
                        continue;
                    }

                    math::MTX34 mtx;
                    CalcTextureMtx(&mtx, pTexSrt[GetTexMtxIdx(places[c])]);
                    memcpy(values.coord[c], &mtx, sizeof(values.coord[c]));
                }
            }

            SiteTexture maps[8];
            bool mapUsed[8] = {false, false, false, false, false, false, false, false};
            if (mNum.texMap > 0) {
                const TexMap* const pTexMap = GetTexMapAry();

                for (int i = 0; i < mNum.texMap && i < 8; i++) {
                    pTexMap[i].Get(&maps[i]);
                    mapUsed[i] = maps[i].mImage != nullptr;
                }
            }

            for (u32 r = 0; r < 3; r++) {
                values.registers[r][0] = mRegisterColors[r].r / 255.0f;
                values.registers[r][1] = mRegisterColors[r].g / 255.0f;
                values.registers[r][2] = mRegisterColors[r].b / 255.0f;
                values.registers[r][3] = mRegisterColors[r].a / 255.0f;
            }
            for (u32 k = 0; k < 4; k++) {
                values.constants[k][0] = mConstantColors[k].r / 255.0f;
                values.constants[k][1] = mConstantColors[k].g / 255.0f;
                values.constants[k][2] = mConstantColors[k].b / 255.0f;
                values.constants[k][3] = mConstantColors[k].a / 255.0f;
            }

            // Each map's size, and each coordinate's scale: the size of the map the cook found it samples.
            for (u32 k = 0; k < 8; k++) {
                values.map_size[k][0] = mapUsed[k] && maps[k].mWidth != 0 ? maps[k].mWidth : 1.0f;
                values.map_size[k][1] = mapUsed[k] && maps[k].mHeight != 0 ? maps[k].mHeight : 1.0f;
                const u32 map = mCoordMap[k];
                const bool known = map < 8 && mapUsed[map];
                values.coord_size[k][0] = known && maps[map].mWidth != 0 ? maps[map].mWidth : values.map_size[k][0];
                values.coord_size[k][1] = known && maps[map].mHeight != 0 ? maps[map].mHeight : values.map_size[k][1];
            }
            if (mHasStages || mNum.texMap == 0) {
                useRasStage = true;
            }

            {
                const TexSRT* const pIndTexSrt = GetIndTexSRTAry();

                for (int i = 0; i < mNum.indSRT && i < 3; i++) {
                    CalcIndTexMtx(values.warp[i], pIndTexSrt[i]);
                }
            }

            if (IsAlphaCompareCap()) {
                const AlphaCompare* const pAlphaComp = GetAlphaComparePtr();
                values.alpha_ref[0] = pAlphaComp->GetRef0() / 255.0f;
                values.alpha_ref[1] = pAlphaComp->GetRef1() / 255.0f;
            }

            PassState state = PassStates::plain();
            state.depth_func = NEBULA_COMPARE_NEVER;
            if (IsBlendModeCap()) {
                const BlendMode* const pBlendMode = GetBlendModePtr();
                const u8 type = pBlendMode->GetType();
                // NEBULA_FACTOR_*: source colour factors 2 and 3 are the destination's colour.
                const u8 src = pBlendMode->GetSrcFactor();
                state.blend_op = type == MODEL_FILE_BM_BLEND ? NEBULA_BLEND_ADD : type == MODEL_FILE_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT : NEBULA_BLEND_REPLACE;
                state.blend_src = src == MODEL_FILE_BL_DSTCLR ? NEBULA_FACTOR_DST_COLOR : src == MODEL_FILE_BL_INVDSTCLR ? NEBULA_FACTOR_INV_DST_COLOR : src;
                state.blend_dst = pBlendMode->GetDstFactor();
            } else {
                state.blend_op = NEBULA_BLEND_ADD;
                state.blend_src = NEBULA_FACTOR_SRC_ALPHA;
                state.blend_dst = NEBULA_FACTOR_INV_SRC_ALPHA;
            }

            const bool remap = mRegisterColors[0] != DefaultBlackColor || !IsWhiteColor(mRegisterColors[1]);
            const UiFormData* pForm = mNativeForms + ((useRasStage ? 1 : 0) | (remap ? 2 : 0));

            const u32 rowCount = static_cast< u32 >(pForm->mVertexRows) + pForm->mPixelRows;
            f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(rowCount * 16, 16));
            DrawMatrixRows coordRows;
            for (u32 i = 0; i < rowCount; i++) {
                const u32 row = pForm->mRows[i];
                layoutRow(row, values, pRows[i]);
                if (row >= NEBULA_ROW_COORD && i < pForm->mVertexRows) {
                    if ((row - NEBULA_ROW_COORD) % 3 == 0) { coordRows.add(i); }
                }
            }
            const u32 constants = Frame::addConstants(NEBULA_NONE32, pForm->mVertexRows, pForm->mPixelRows, pRows, coordRows.data(), coordRows.count());

            NebulaFrameTexture list[NEBULA_MATERIAL_SLOTS];
            for (u32 i = 0; i < pForm->mTextureCount; i++) {
                const u32 map = pForm->mTextures[i];
                if (map < 8 && mapUsed[map]) {
                    SiteTextures::toFrame(maps[map], &list[i]);
                } else {
                    memset(&list[i], 0, sizeof(list[i]));
                }
            }
            const NebulaHostServices* pHost = nebula_host();
            const u32 table = pForm->mTextureCount != 0 ? pHost->texture_table(pHost->user, list, pForm->mTextureCount) : 0;

            // A layout material is the last word on ambient: models after it that keep the ambient in force read black.
            const NebulaColor8 black = {0, 0, 0, 0};
            MaterialAmbient::set(0, black);
            MaterialAmbient::set(1, black);

            detail::SetDrawForm(pForm, constants, table, state.word());

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
            const Size GetTextureSize(Material* pMaterial, u8 idx) {
                if (idx >= pMaterial->GetTextureNum()) {
                    return Size(0.0f, 0.0f);
                }

                const TexMap& rTexMap = pMaterial->GetTexture(idx);
                return rTexMap.GetSize();
            }
        };  // namespace detail

    };  // namespace lyt
};  // namespace nw4r
