#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "nw4r/lyt/animation.h"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <nebula_game_api.h>
#include <string.h>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
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
                return (mtx - GPU_TEXMTX0) / 3;
            }

            inline u32 GetTexMtx(u32 idx) {
                return GPU_TEXMTX0 + idx * 3;
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

            u32 CalcOffsetTexSRTAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.texMap * ROUND_UP(sizeof(nw4r::lyt::TexMap), 4);
            }

            u32 CalcOffsetTexCoordGenAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.texSRT * ROUND_UP(sizeof(nw4r::lyt::TexSRT), 4) + CalcOffsetTexSRTAry(rNums);
            }

            u32 CalcOffsetChanCtrlAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.texCoordGen * ROUND_UP(sizeof(nw4r::lyt::TexCoordGen), 4) + CalcOffsetTexCoordGenAry(rNums);
            }

            u32 CalcOffsetMatColAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.chanCtrl * ROUND_UP(sizeof(nw4r::lyt::ChanCtrl), 4) + CalcOffsetChanCtrlAry(rNums);
            }

            u32 CalcOffsetTevSwapAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.matCol * ROUND_UP(sizeof(nw4r::ut::Color), 4) + CalcOffsetMatColAry(rNums);
            }

            u32 CalcOffsetGetAlphaCompare(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.tevSwap * ROUND_UP(sizeof(nw4r::lyt::TevSwapMode), 4) + CalcOffsetTevSwapAry(rNums);
            }

            u32 CalcOffsetBlendMode(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.alpComp * ROUND_UP(sizeof(nw4r::lyt::AlphaCompare), 4) + CalcOffsetGetAlphaCompare(rNums);
            }

            u32 CalcOffsetIndirectStageAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.blendMode * ROUND_UP(sizeof(nw4r::lyt::BlendMode), 4) + CalcOffsetBlendMode(rNums);
            }

            u32 CalcOffsetIndTexSRTAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.indStage * ROUND_UP(sizeof(nw4r::lyt::IndirectStage), 4) + CalcOffsetIndirectStageAry(rNums);
            }

            u32 CalcOffsetTevStageAry(const nw4r::lyt::detail::BitGXNums& rNums) {
                return rNums.indSRT * ROUND_UP(sizeof(nw4r::lyt::TexSRT), 4) + CalcOffsetIndTexSRTAry(rNums);
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

#pragma dont_inline on

        // inlining is off the charts here. No idea why the compiler will not inline so much of these calls
        Material::Material(const res::Material* pRes, const ResBlockSet& resBlockSet) {
            Init();

            SetName(pRes->name);

            for (int i = 0; i < 3; ++i) {
                mTevCols[i] = pRes->tevCols[i];
            }

            for (int i = 0; i < 4; ++i) {
                mTevKCols[i] = pRes->tevKCols[i];
            }

            u32 resOffs = sizeof(res::Material);
            const res::TexMap* const pResTexMap = detail::ConvertOffsToPtr< res::TexMap >(pRes, resOffs);
            resOffs += sizeof(res::TexMap) * pRes->resNum.GetTexMapNum();
            const TexSRT* const resTexSRTs = detail::ConvertOffsToPtr< TexSRT >(pRes, resOffs);
            resOffs += sizeof(TexSRT) * pRes->resNum.GetTexSRTNum();
            const TexCoordGen* const resTexCoordGens = detail::ConvertOffsToPtr< TexCoordGen >(pRes, resOffs);
            resOffs += sizeof(TexCoordGen) * pRes->resNum.GetTexCoordGenNum();

            const u8 texMapNum = ut::Min(pRes->resNum.GetTexMapNum(), u8(GPU_MAX_TEXMAP));
            const u8 texSRTNum = ut::Min(pRes->resNum.GetTexSRTNum(), u8(10));
            const u8 texCoordGenNum = ut::Min(pRes->resNum.GetTexCoordGenNum(), u8(GPU_MAX_TEXCOORD));
            const bool allocChanCtrl = 0 != pRes->resNum.GetChanCtrlNum();
            const bool allocMatCol = 0 != pRes->resNum.GetMatColNum();
            const bool allocTevSwap = pRes->resNum.HasTevSwapTable();
            const bool allocAlpComp = pRes->resNum.HasAlphaCompare();
            const bool allocBlendMode = pRes->resNum.HasBlendMode();
            u8 indTexSRTNum = ut::Min(pRes->resNum.GetIndTexSRTNum(), u8(3));
            u8 indStageNum = ut::Min(pRes->resNum.GetIndTexStageNum(), u8(GPU_MAX_INDTEXSTAGE));
            u8 tevStageNum = ut::Min(pRes->resNum.GetTevStageNum(), u8(GPU_MAX_TEVSTAGE));

            ReserveGXMem(texMapNum, texSRTNum, texCoordGenNum, tevStageNum, allocTevSwap, indStageNum, indTexSRTNum, allocChanCtrl, allocMatCol,
                         allocAlpComp, allocBlendMode);

            if (mpGXMem) {
                SetTextureNum(texMapNum);
                if (texMapNum > 0) {
                    const res::Texture* const textures =
                        detail::ConvertOffsToPtr< res::Texture >(resBlockSet.pTextureList, sizeof(*resBlockSet.pTextureList));
                    TexMap* const texMaps = GetTexMapAry();

                    u8 di = 0;
                    for (u8 si = 0; si < texMapNum; ++si) {
                        const char* const fileName = detail::ConvertOffsToPtr< char >(textures, textures[pResTexMap[si].texIdx].nameStrOffset);

                        void* const pTplRes = resBlockSet.pResAccessor->GetResource(0x74696D67, fileName, nullptr);
                        texMaps[di].ReplaceImage(pTplRes, 0);
                        texMaps[di].SetWrapMode(pResTexMap[si].GetWarpModeS(), pResTexMap[si].GetWarpModeT());
                        texMaps[di].SetFilter(pResTexMap[si].GetMinFilter(), pResTexMap[si].GetMagFilter());
                        ++di;
                    }
                    SetTextureNum(di);
                }

                TexSRT* texSRTs = GetTexSRTAry();
                for (int i = 0; i < texSRTNum; ++i) {
                    texSRTs[i].translate = resTexSRTs[i].translate;
                    texSRTs[i].rotate = resTexSRTs[i].rotate;
                    texSRTs[i].scale = resTexSRTs[i].scale;
                }

                TexCoordGen* texCoordGens = GetTexCoordGenAry();
                SetTexCoordGenNum(texCoordGenNum);
                for (int i = 0; i < mGXMemNum.texCoordGen; ++i) {
                    texCoordGens[i] = resTexCoordGens[i];
                }

                if (allocChanCtrl) {
                    const ChanCtrl* const pResChanCtrl = detail::ConvertOffsToPtr< ChanCtrl >(pRes, resOffs);
                    GetChanCtrlAry()[0] = *pResChanCtrl;
                    resOffs += sizeof(*pResChanCtrl);
                }

                if (allocMatCol) {
                    const ut::Color* const pResMatCol = detail::ConvertOffsToPtr< ut::Color >(pRes, resOffs);
                    GetMatColAry()[0] = *pResMatCol;
                    resOffs += sizeof(*pResMatCol);
                }

                if (allocTevSwap) {
                    const TevSwapMode* const pResTevSwap = detail::ConvertOffsToPtr< TevSwapMode >(pRes, resOffs);
                    TevSwapMode* const tevSwaps = GetTevSwapAry();
                    for (int i = 0; i < GPU_MAX_TEVSWAP; ++i) {
                        tevSwaps[i] = pResTevSwap[i];
                    }
                    resOffs += sizeof(*tevSwaps) * GPU_MAX_TEVSWAP;
                }

                if (indTexSRTNum > 0) {
                    TexSRT* const indTexSRTs = GetIndTexSRTAry();
                    const TexSRT* const pResIndMtx = detail::ConvertOffsToPtr< TexSRT >(pRes, resOffs);
                    for (int i = 0; i < indTexSRTNum; ++i) {
                        indTexSRTs[i] = pResIndMtx[i];
                    }
                }
                resOffs += sizeof(TexSRT) * pRes->resNum.GetIndTexSRTNum();

                if (indStageNum > 0) {
                    SetIndStageNum(indStageNum);
                    IndirectStage* indirectStages = GetIndirectStageAry();
                    const IndirectStage* const pResIndStg = detail::ConvertOffsToPtr< IndirectStage >(pRes, resOffs);
                    for (int i = 0; i < indStageNum; ++i) {
                        indirectStages[i] = pResIndStg[i];
                    }
                }
                resOffs += sizeof(IndirectStage) * pRes->resNum.GetIndTexStageNum();

                if (tevStageNum > 0) {
                    SetTevStageNum(tevStageNum);
                    TevStage* const tevStages = GetTevStageAry();
                    const TevStage* const pResTevStg = detail::ConvertOffsToPtr< TevStage >(pRes, resOffs);
                    for (int i = 0; i < tevStageNum; ++i) {
                        tevStages[i] = pResTevStg[i];
                    }
                }
                resOffs += sizeof(TevStage) * pRes->resNum.GetTevStageNum();

                if (allocAlpComp) {
                    const AlphaCompare* const pResAlphaCompare = detail::ConvertOffsToPtr< AlphaCompare >(pRes, resOffs);
                    *GetAlphaComparePtr() = *pResAlphaCompare;
                    resOffs += sizeof(*pResAlphaCompare);
                }

                if (allocBlendMode) {
                    const BlendMode* const pResBlendMode = detail::ConvertOffsToPtr< BlendMode >(pRes, resOffs);
                    *GetBlendModePtr() = *pResBlendMode;
                    resOffs += sizeof(*pResBlendMode);
                }
            }
            const u8* native = detail::ConvertOffsToPtr<u8>(pRes, resOffs);
            if (memcmp(native, "NSID", 4) == 0) {
                memcpy(mNativeShaders, native + 4, sizeof(mNativeShaders));
            }
        }

#pragma dont_inline reset

        void Material::Init() {
            mTevCols[0] = DefaultBlackColor;
            mTevCols[1].a = mTevCols[1].b = mTevCols[1].g = mTevCols[1].r = 255;
            mTevCols[2].a = mTevCols[2].b = mTevCols[2].g = mTevCols[2].r = 255;

            InitBitGXNums(&mGXMemCap);
            InitBitGXNums(&mGXMemNum);

            mbUserAllocated = false;

            mpGXMem = 0;
            memset(mNativeShaders, 0, sizeof(mNativeShaders));
        }

        Material::~Material() {
            UnbindAllAnimation();

            if (mpGXMem != NULL) {
                Layout::FreeMemory(mpGXMem);
                mpGXMem = NULL;
            }
        }

        void Material::InitBitGXNums(detail::BitGXNums* ptr) {
            ptr->texMap = 0;
            ptr->texSRT = 0;
            ptr->texCoordGen = 0;
            ptr->indSRT = 0;
            ptr->indStage = 0;
            ptr->tevSwap = 0;
            ptr->tevStage = 0;
            ptr->chanCtrl = 0;
            ptr->matCol = 0;
            ptr->alpComp = 0;
            ptr->blendMode = 0;
        }

        void Material::ReserveGXMem(u8 texMapNum, u8 texSRTNum, u8 texCoordGenNum, u8 tevStageNum, bool allocTevSwap, u8 indStageNum, u8 indSRTNum,
                                    bool allocChanCtrl, bool allocMatCol, bool allocAlpComp, bool allocBlendMode) {
            const int tevSwapNum = allocTevSwap ? 1 : 0;
            const int chanCtrlNum = allocChanCtrl ? 1 : 0;
            const int matColNum = allocMatCol ? 1 : 0;
            const int alpCompNum = allocAlpComp ? 1 : 0;
            const int blendModeNum = allocBlendMode ? 1 : 0;

            if (mGXMemCap.texMap < texMapNum || mGXMemCap.texSRT < texSRTNum || mGXMemCap.texCoordGen < texCoordGenNum ||
                mGXMemCap.tevStage < tevStageNum || mGXMemCap.tevSwap < tevSwapNum || mGXMemCap.indStage < indStageNum ||
                mGXMemCap.indSRT < indSRTNum || mGXMemCap.chanCtrl < chanCtrlNum || mGXMemCap.matCol < matColNum || mGXMemCap.alpComp < alpCompNum ||
                mGXMemCap.blendMode < blendModeNum) {
                if (mpGXMem) {
                    Layout::FreeMemory(mpGXMem);
                    mpGXMem = 0;

                    InitBitGXNums(&mGXMemCap);
                    InitBitGXNums(&mGXMemNum);
                }

                mpGXMem = Layout::AllocMemory(sizeof(TexMap) * texMapNum + sizeof(TexSRT) * texSRTNum +
                                              4 * (texCoordGenNum + chanCtrlNum + matColNum + tevSwapNum + alpCompNum + blendModeNum + indStageNum) +
                                              sizeof(TexSRT) * indSRTNum + sizeof(TevStage) * tevStageNum);
                if (mpGXMem) {
                    mGXMemCap.texMap = texMapNum;
                    mGXMemCap.texSRT = texSRTNum;
                    mGXMemCap.texCoordGen = texCoordGenNum;
                    mGXMemCap.indSRT = indSRTNum;
                    mGXMemCap.indStage = indStageNum;
                    mGXMemCap.tevSwap = tevSwapNum;
                    mGXMemCap.tevStage = tevStageNum;
                    mGXMemCap.chanCtrl = chanCtrlNum;
                    mGXMemCap.matCol = matColNum;
                    mGXMemCap.alpComp = alpCompNum;
                    mGXMemCap.blendMode = blendModeNum;

                    mGXMemNum.texSRT = mGXMemCap.texSRT;
                    InitTexSRT(GetTexSRTAry(), mGXMemNum.texSRT);
                    mGXMemNum.indSRT = mGXMemCap.indSRT;
                    InitTexSRT(GetIndTexSRTAry(), mGXMemNum.indSRT);

                    mGXMemNum.chanCtrl = mGXMemCap.chanCtrl;
                    if (mGXMemNum.chanCtrl) {
                        GetChanCtrlAry()[0] = ChanCtrl();
                    }

                    mGXMemNum.matCol = mGXMemCap.matCol;
                    if (mGXMemNum.matCol) {
                        GetMatColAry()[0] = ut::Color::WHITE;
                    }

                    mGXMemNum.tevSwap = mGXMemCap.tevSwap;
                    if (mGXMemNum.tevSwap) {
                        TevSwapMode* const tevSwaps = GetTevSwapAry();
                        tevSwaps[0].Set(GPU_CH_RED, GPU_CH_GREEN, GPU_CH_BLUE, GPU_CH_ALPHA);
                        tevSwaps[1].Set(GPU_CH_RED, GPU_CH_RED, GPU_CH_RED, GPU_CH_ALPHA);
                        tevSwaps[2].Set(GPU_CH_GREEN, GPU_CH_GREEN, GPU_CH_GREEN, GPU_CH_ALPHA);
                        tevSwaps[3].Set(GPU_CH_BLUE, GPU_CH_BLUE, GPU_CH_BLUE, GPU_CH_ALPHA);
                    }

                    mGXMemNum.alpComp = mGXMemCap.alpComp;
                    if (mGXMemNum.alpComp) {
                        *GetAlphaComparePtr() = AlphaCompare();
                    }

                    mGXMemNum.blendMode = mGXMemCap.blendMode;
                    if (mGXMemNum.blendMode) {
                        *GetBlendModePtr() = BlendMode();
                    }
                }
            }
        }

        const TexMap* Material::GetTexMapAry() const {
            return detail::ConvertOffsToPtr< TexMap >(mpGXMem, 0);
        }

        TexMap* Material::GetTexMapAry() {
            return detail::ConvertOffsToPtr< TexMap >(mpGXMem, 0);
        }

        TexSRT* Material::GetTexSRTAry() {
            return detail::ConvertOffsToPtr< TexSRT >(mpGXMem, CalcOffsetTexSRTAry(mGXMemCap));
        }

        TexCoordGen* Material::GetTexCoordGenAry() {
            return detail::ConvertOffsToPtr< TexCoordGen >(mpGXMem, CalcOffsetTexCoordGenAry(mGXMemCap));
        }

        ChanCtrl* Material::GetChanCtrlAry() {
            return detail::ConvertOffsToPtr< ChanCtrl >(mpGXMem, CalcOffsetChanCtrlAry(mGXMemCap));
        }

        ut::Color* Material::GetMatColAry() {
            return detail::ConvertOffsToPtr< ut::Color >(mpGXMem, CalcOffsetMatColAry(mGXMemCap));
        }

        TevSwapMode* Material::GetTevSwapAry() {
            return detail::ConvertOffsToPtr< TevSwapMode >(mpGXMem, CalcOffsetTevSwapAry(mGXMemCap));
        }

        AlphaCompare* Material::GetAlphaComparePtr() {
            return detail::ConvertOffsToPtr< AlphaCompare >(mpGXMem, CalcOffsetGetAlphaCompare(mGXMemCap));
        }

        BlendMode* Material::GetBlendModePtr() {
            return detail::ConvertOffsToPtr< BlendMode >(mpGXMem, CalcOffsetBlendMode(mGXMemCap));
        }

        IndirectStage* Material::GetIndirectStageAry() {
            return detail::ConvertOffsToPtr< IndirectStage >(mpGXMem, CalcOffsetIndirectStageAry(mGXMemCap));
        }

        TexSRT* Material::GetIndTexSRTAry() {
            return detail::ConvertOffsToPtr< TexSRT >(mpGXMem, CalcOffsetIndTexSRTAry(mGXMemCap));
        }

        TevStage* Material::GetTevStageAry() {
            return detail::ConvertOffsToPtr< TevStage >(mpGXMem, CalcOffsetTevStageAry(mGXMemCap));
        }

        void Material::SetName(const char* pName) {
            strncpy(mName, pName, sizeof(mName) - 1);
            mName[sizeof(mName) - 1] = '\0';
        }

        void Material::SetTextureNum(u8 num) {
            if (num > 0) {
                TexMap* const pTexMap = GetTexMapAry();

                for (u32 i = mGXMemNum.texMap; i < num; i++) {
                    new (&pTexMap[i]) TexMap();
                }
            }

            mGXMemNum.texMap = num;
        }

        void Material::SetTexCoordGenNum(u8 num) {
            if (num > 0) {
                TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

                for (u32 i = mGXMemNum.texCoordGen; i < num; i++) {
                    new (&pTexCoordGen[i]) TexCoordGen();
                }
            }

            mGXMemNum.texCoordGen = num;
        }

        void Material::SetTevStageNum(u8 num) {
            if (num > 0) {
                TevStage* const pTevStage = GetTevStageAry();

                for (u32 i = mGXMemNum.tevStage; i < num; i++) {
                    new (&pTevStage[i]) TevStage();
                }
            }

            mGXMemNum.tevStage = num;
        }

        void Material::SetIndStageNum(u8 num) {
            if (num > 0) {
                IndirectStage* const pIndStage = GetIndirectStageAry();

                for (u32 i = mGXMemNum.indStage; i < num; i++) {
                    new (&pIndStage[i]) IndirectStage();
                }
            }

            mGXMemNum.indStage = num;
        }

        void Material::SetColorElement(u32 idx, s16 value) {
            switch (idx) {
            case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_R:
            case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_G:
            case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_B:
            case detail::ANIMTARGET_MATCOLOR_MATCOLOR0_A:
                if (mGXMemNum.matCol >= 1) {
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
                    mTevCols[regIdx].r = value;
                    break;
                case 1:
                    mTevCols[regIdx].g = value;
                    break;
                case 2:
                    mTevCols[regIdx].b = value;
                    break;
                case 3:
                    mTevCols[regIdx].a = value;
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
                SetColorComponentValue(&mTevKCols[regIdx], (idx - detail::ANIMTARGET_MATCOLOR_TEVKONST0_R) % 4, value);
            } break;
            }
        }

        bool Material::SetupGX(bool modulate, u8 alpha) {
            // The material's setup and values, drawn by the shader the cook chose (mNativeShaders). Stored until the next
            // material.
            static J3DMaterialSetup sSetup;
            static J3DMaterialValues sValues;
            J3DMaterialSetup& rSetup = sSetup;
            J3DMaterialValues& rValues = sValues;
            memset(&rSetup, 0, sizeof(rSetup));
            memset(&rValues, 0, sizeof(rValues));
            rSetup.loads = J3D_LOADS_TEV_COLOR | J3D_LOADS_KONST_COLOR | J3D_LOADS_AMBIENT | J3D_LOADS_CHANNELS | J3D_LOADS_INDIRECT;
            rSetup.cull = NEBULA_CULL_NONE;
            bool useVtxColor = true;
            bool useMatColor = false;

            // Channel 0 unlit: its colour and alpha from the material colour or the vertex (GpuSetChanCtrl's material
            // source; the ambient source the register).
            if (IsChanCtrlCap()) {
                const ChanCtrl* const pChanCtrl = GetChanCtrlAry();
                useVtxColor = pChanCtrl->GetColorSrc() == GPU_SRC_VTX || pChanCtrl->GetAlphaSrc() == GPU_SRC_VTX;
                useMatColor = pChanCtrl->GetColorSrc() == GPU_SRC_REG || pChanCtrl->GetAlphaSrc() == GPU_SRC_REG;
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
                rValues.material_color[0][0] = matColor.r;
                rValues.material_color[0][1] = matColor.g;
                rValues.material_color[0][2] = matColor.b;
                rValues.material_color[0][3] = matColor.a;

                useRasStage = useRasStage || matColor != ut::Color::WHITE;
            } else {
                rValues.material_color[0][0] = rValues.material_color[0][1] = rValues.material_color[0][2] =
                    rValues.material_color[0][3] = 255;
            }

            bool setTexMtx = false;
            bool useTexMtx[10];

            for (int i = 0; i < 10; i++) {
                useTexMtx[i] = false;
            }

            u8 places[8] = {GPU_IDENTITY, GPU_IDENTITY, GPU_IDENTITY, GPU_IDENTITY,
                            GPU_IDENTITY, GPU_IDENTITY, GPU_IDENTITY, GPU_IDENTITY};
            rSetup.tex_gens = mGXMemNum.texCoordGen;
            if (mGXMemNum.texCoordGen > 0) {
                const TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

                for (int i = 0; i < mGXMemNum.texCoordGen; i++) {
                    u32 texMtx = pTexCoordGen[i].GetTexMtx();

                    if (pTexCoordGen[i].GetTexGenType() == GX_TG_MTX2x4 && texMtx != GPU_IDENTITY) {
                        useTexMtx[GetTexMtxIdx(texMtx)] = true;
                        setTexMtx = true;
                    }

                    if (i < 8) {
                        rSetup.tex_gen_post[i] = static_cast< u8 >(GPU_PTIDENTITY);
                    }
                    if (i < 8) {
                        places[i] = static_cast< u8 >(texMtx);
                    }
                }
            }

            if (setTexMtx) {
                const TexSRT* const pTexSrt = GetTexSRTAry();

                for (u8 i = 0; i < mGXMemNum.texSRT; i++) {
                    if (!useTexMtx[i]) {
                        continue;
                    }

                    math::MTX34 mtx;
                    CalcTextureMtx(&mtx, pTexSrt[i]);
                    J3DNativeShape::setTexture((GetTexMtx(i) - GPU_TEXMTX0) / 3, mtx, 2);
                }
            }

            if (mGXMemNum.texMap > 0) {
                const TexMap* const pTexMap = GetTexMapAry();

                for (int i = 0; i < mGXMemNum.texMap && i < 8; i++) {
                    GpuTexObj texObj;
                    pTexMap[i].Get(&texObj);
                    void* pImage;
                    u16 width, height;
                    GpuTexFmt format;
                    GpuTexWrapMode wrapS, wrapT;
                    GpuBool mipmap;
                    J3DNativeTexture::getAll(&texObj, &pImage, &width, &height, &format, &wrapS, &wrapT, &mipmap);
                    GpuTexFilter minFilter, magFilter;
                    f32 minLod, maxLod, lodBias;
                    GpuBool biasClamp, edgeLod;
                    GpuAnisotropy maxAniso;
                    J3DNativeTexture::getLODAll(&texObj, &minFilter, &magFilter, &minLod, &maxLod, &lodBias, &biasClamp, &edgeLod,
                                                &maxAniso);
                    NebulaMaterialTexture& rTex = rValues.texture[i];
                    rTex.image = reinterpret_cast< uintptr_t >(pImage);
                    rTex.width = width;
                    rTex.height = height;
                    rTex.wrap_s = static_cast< u8 >(wrapS);
                    rTex.wrap_t = static_cast< u8 >(wrapT);
                    rTex.min_linear = minFilter == GPU_LINEAR || minFilter == GPU_LIN_MIP_NEAR || minFilter == GPU_LIN_MIP_LIN;
                    rTex.mag_linear = magFilter != GPU_NEAR;
                    rTex.mip_filter = minFilter < GPU_NEAR_MIP_NEAR ? 0 : minFilter < GPU_NEAR_MIP_LIN ? 1 : 2;
                    rTex.edge_lod = edgeLod ? 1 : 0;
                    rTex.anisotropy = static_cast< u8 >(maxAniso);
                    rTex.min_lod = minLod;
                    rTex.max_lod = maxLod;
                    rTex.lod_bias = lodBias;
                }
            }

            for (u32 r = 0; r < 3; r++) {
                rValues.tev_color[r][0] = mTevCols[r].r;
                rValues.tev_color[r][1] = mTevCols[r].g;
                rValues.tev_color[r][2] = mTevCols[r].b;
                rValues.tev_color[r][3] = mTevCols[r].a;
            }
            for (u32 k = 0; k < 4; k++) {
                rValues.konst_color[k][0] = mTevKCols[k].r;
                rValues.konst_color[k][1] = mTevKCols[k].g;
                rValues.konst_color[k][2] = mTevKCols[k].b;
                rValues.konst_color[k][3] = mTevKCols[k].a;
            }

            bool setIndTexMtx = false;
            bool useIndTexMtx[3];

            for (int i = 0; i < 3; i++) {
                useIndTexMtx[i] = false;
            }

            // Each coordinate's scale: the size of the texture its stage samples.
            const auto coordScale = [&rValues](u32 coord, u32 map) {
                if (coord < 8 && map < 8 && rValues.texture[map].image != 0) {
                    rValues.tex_coord_scale[coord].scale_s = rValues.texture[map].width;
                    rValues.tex_coord_scale[coord].scale_t = rValues.texture[map].height;
                }
            };
            if (mGXMemNum.tevStage > 0) {
                const TevStage* const pTevStage = GetTevStageAry();

                for (int i = 0; i < mGXMemNum.tevStage && i < 16; i++) {
                    const TevStage& rSrc = pTevStage[i];
                    coordScale(rSrc.GetTexCoordGen(), rSrc.GetTexMap());
                    GpuIndTexMtxID indMtx = rSrc.GetIndMtxSel();
                    if (GPU_ITM_0 <= indMtx && indMtx <= GPU_ITM_2) {
                        useIndTexMtx[indMtx - 1] = true;
                        setIndTexMtx = true;
                    }
                }

                useRasStage = true;
            } else if (mGXMemNum.texMap == 0) {
                useRasStage = true;
            } else {
                // nw4r's stages for a material without its own: each texture by its own coordinate.
                for (int i = 0; i < mGXMemNum.texMap; i++) {
                    coordScale(i, i);
                }
            }

            if (setIndTexMtx) {
                const TexSRT* const pIndTexSrt = GetIndTexSRTAry();

                for (int i = 0; i < mGXMemNum.indSRT && i < 3; i++) {
                    if (!useIndTexMtx[i]) {
                        continue;
                    }

                    f32 mtx[2][3];
                    CalcIndTexMtx(mtx, pIndTexSrt[i]);
                    for (u32 row = 0; row < 2; row++) {
                        for (u32 c = 0; c < 3; c++) {
                            rValues.ind_matrix[i].m[row][c] = mtx[row][c];
                        }
                    }
                }
            }

            if (mGXMemNum.indStage > 0) {
                const IndirectStage* const pIndStage = GetIndirectStageAry();

                for (int i = 0; i < mGXMemNum.indStage && i < 4; i++) {
                    coordScale(pIndStage[i].GetTexCoordGen(), pIndStage[i].GetTexMap());
                }
            }

            if (IsAlphaCompareCap()) {
                const AlphaCompare* const pAlphaComp = GetAlphaComparePtr();

                rSetup.alpha_compare.comp0 = pAlphaComp->GetComp0();
                rSetup.alpha_compare.op = pAlphaComp->GetOp();
                rSetup.alpha_compare.comp1 = pAlphaComp->GetComp1();
                rValues.alpha_ref[0] = pAlphaComp->GetRef0();
                rValues.alpha_ref[1] = pAlphaComp->GetRef1();
            } else {
                rSetup.alpha_compare.comp0 = GPU_ALWAYS;
                rSetup.alpha_compare.op = GPU_AOP_AND;
                rSetup.alpha_compare.comp1 = GPU_ALWAYS;
            }

            if (IsBlendModeCap()) {
                const BlendMode* const pBlendMode = GetBlendModePtr();

                {
                    const u8 type = pBlendMode->GetType();
                    rSetup.blend.type = type == GPU_BM_BLEND ? NEBULA_BLEND_ADD : type == GPU_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                                          : type == GPU_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
                }
                {
                    // NEBULA_FACTOR_*: GX's source colour factors (2, 3) are the destination's colour.
                    const u8 src = pBlendMode->GetSrcFactor();
                    rSetup.blend.source = src == GPU_BL_DSTCLR ? NEBULA_FACTOR_DST_COLOR
                                            : src == GPU_BL_INVDSTCLR ? NEBULA_FACTOR_INV_DST_COLOR : src;
                }
                rSetup.blend.destination = pBlendMode->GetDstFactor();
                rSetup.blend.logic_op = pBlendMode->GetOp();
            } else {
                rSetup.blend.type = NEBULA_BLEND_ADD;
                rSetup.blend.source = NEBULA_FACTOR_SRC_ALPHA;
                rSetup.blend.destination = NEBULA_FACTOR_INV_SRC_ALPHA;
                rSetup.blend.logic_op = GPU_LO_SET;
            }

            const bool remap = mTevCols[0] != DefaultBlackColor || !IsWhiteColor(mTevCols[1]);
            const u32 shader = mNativeShaders[(useRasStage ? 1 : 0) | (remap ? 2 : 0)];
            J3DNativeMaterial::sendWhole(shader != 0 ? NEBULA_SHADER_J3D_FIRST + shader - 1 : 0, rSetup, rValues);
            J3DNativeShape::setCurrent(GPU_PNMTX0, places);

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
            for (AnimationList::Iterator it = mAnimList.GetBeginIter(); it != mAnimList.GetEndIter(); ++it) {
                if (it->IsEnable()) {
                    AnimTransform* animTrans = it->GetAnimTransform();
                    animTrans->Animate(it->GetIndex(), this);
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
