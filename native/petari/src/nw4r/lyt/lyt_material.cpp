#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "nw4r/lyt/animation.h"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <nebula_game_api.h>
#include <string.h>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Util/NativeShader.hpp"
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
                return (mtx - J3D_TEXMTX0) / 3;
            }

            inline u32 GetTexMtx(u32 idx) {
                return J3D_TEXMTX0 + idx * 3;
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


        // inlining is off the charts here. No idea why the compiler will not inline so much of these calls
        Material::Material(const res::Material* pRes, const ResBlockSet& resBlockSet) {
            Init();

            SetName(pRes->name);

            for (int i = 0; i < 3; ++i) {
                mRegisterColors[i] = pRes->tevCols[i];
            }

            for (int i = 0; i < 4; ++i) {
                mConstantColors[i] = pRes->tevKCols[i];
            }

            u32 resOffs = sizeof(res::Material);
            const res::TexMap* const pResTexMap = detail::ConvertOffsToPtr< res::TexMap >(pRes, resOffs);
            resOffs += sizeof(res::TexMap) * pRes->resNum.GetTexMapNum();
            const TexSRT* const resTexSRTs = detail::ConvertOffsToPtr< TexSRT >(pRes, resOffs);
            resOffs += sizeof(TexSRT) * pRes->resNum.GetTexSRTNum();
            const TexCoordGen* const resTexCoordGens = detail::ConvertOffsToPtr< TexCoordGen >(pRes, resOffs);
            resOffs += sizeof(TexCoordGen) * pRes->resNum.GetTexCoordGenNum();

            const u8 texMapNum = ut::Min(pRes->resNum.GetTexMapNum(), u8(8));
            const u8 texSRTNum = ut::Min(pRes->resNum.GetTexSRTNum(), u8(10));
            const u8 texCoordGenNum = ut::Min(pRes->resNum.GetTexCoordGenNum(), u8(8));
            const bool allocChanCtrl = 0 != pRes->resNum.GetChanCtrlNum();
            const bool allocMatCol = 0 != pRes->resNum.GetMatColNum();
            const bool allocAlpComp = pRes->resNum.HasAlphaCompare();
            const bool allocBlendMode = pRes->resNum.HasBlendMode();
            u8 indTexSRTNum = ut::Min(pRes->resNum.GetIndTexSRTNum(), u8(3));

            ReserveStorage(texMapNum, texSRTNum, texCoordGenNum, indTexSRTNum, allocChanCtrl, allocMatCol,
                         allocAlpComp, allocBlendMode);

            if (mpStorage) {
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
                for (int i = 0; i < mNum.texCoordGen; ++i) {
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

                if (indTexSRTNum > 0) {
                    TexSRT* const indTexSRTs = GetIndTexSRTAry();
                    const TexSRT* const pResIndMtx = detail::ConvertOffsToPtr< TexSRT >(pRes, resOffs);
                    for (int i = 0; i < indTexSRTNum; ++i) {
                        indTexSRTs[i] = pResIndMtx[i];
                    }
                }
                resOffs += sizeof(TexSRT) * pRes->resNum.GetIndTexSRTNum();

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
                memcpy(mCoordMap, native + 4 + sizeof(mNativeShaders), sizeof(mCoordMap));
                mHasStages = native[4 + sizeof(mNativeShaders) + sizeof(mCoordMap)] != 0;
            }
        }


        void Material::Init() {
            mRegisterColors[0] = DefaultBlackColor;
            mRegisterColors[1].a = mRegisterColors[1].b = mRegisterColors[1].g = mRegisterColors[1].r = 255;
            mRegisterColors[2].a = mRegisterColors[2].b = mRegisterColors[2].g = mRegisterColors[2].r = 255;

            InitStorageNums(&mCap);
            InitStorageNums(&mNum);

            mbUserAllocated = false;

            mpStorage = 0;
            memset(mNativeShaders, 0, sizeof(mNativeShaders));
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
            // A picture made in code has no material in a layout file, so no shader from the cook: J2DPicture's own.
            if (mNativeShaders[0] == 0 && mNum.texMap != 0) {
                J3DNativeTexObj texObj;
                GetTexMapAry()[0].Get(&texObj);
                NebulaNativeMaterial material;
                NativeShader::init(material, NEBULA_SHADER_J2D_PICTURE, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS,
                                   false, NEBULA_CULL_NONE);
                NativeShader::setColor(material, 12, 255, 255, 255, 255);
                material.constants[16] = 1.0f;
                material.constants[17] = 1.0f;
                material.textures[0].image = reinterpret_cast< uintptr_t >(texObj.mImage);
                material.textures[0].repeat_s = texObj.mWrapS;
                material.textures[0].repeat_t = texObj.mWrapT;
                material.textures[0].mag_linear = 1;
                material.textures[0].min_linear = 1;
                NativeShader::send(material);
                return true;
            }

            // The material's setup and values, drawn by the shader the cook chose (mNativeShaders). Stored until the next
            // material.
            static J3DMaterialSetup sSetup;
            static J3DMaterialValues sValues;
            J3DMaterialSetup& rSetup = sSetup;
            J3DMaterialValues& rValues = sValues;
            memset(&rSetup, 0, sizeof(rSetup));
            memset(&rValues, 0, sizeof(rValues));
            rSetup.loads = J3D_LOADS_REGISTER_COLOR | J3D_LOADS_KONST_COLOR | J3D_LOADS_AMBIENT | J3D_LOADS_CHANNELS | J3D_LOADS_INDIRECT;
            rSetup.cull = NEBULA_CULL_NONE;
            bool useVtxColor = true;
            bool useMatColor = false;

            // Channel 0 unlit: its colour and alpha from the material colour or the vertex (the channel's material
            // source; the ambient source the register).
            if (IsChanCtrlCap()) {
                const ChanCtrl* const pChanCtrl = GetChanCtrlAry();
                useVtxColor = pChanCtrl->GetColorSrc() == J3D_SRC_VTX || pChanCtrl->GetAlphaSrc() == J3D_SRC_VTX;
                useMatColor = pChanCtrl->GetColorSrc() == J3D_SRC_REG || pChanCtrl->GetAlphaSrc() == J3D_SRC_REG;
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

            u8 places[8] = {J3D_IDENTITY, J3D_IDENTITY, J3D_IDENTITY, J3D_IDENTITY,
                            J3D_IDENTITY, J3D_IDENTITY, J3D_IDENTITY, J3D_IDENTITY};
            rSetup.tex_gens = mNum.texCoordGen;
            if (mNum.texCoordGen > 0) {
                const TexCoordGen* const pTexCoordGen = GetTexCoordGenAry();

                for (int i = 0; i < mNum.texCoordGen; i++) {
                    u32 texMtx = pTexCoordGen[i].GetTexMtx();

                    if (pTexCoordGen[i].GetTexGenType() == J3D_TG_MTX2x4 && texMtx != J3D_IDENTITY) {
                        useTexMtx[GetTexMtxIdx(texMtx)] = true;
                        setTexMtx = true;
                    }

                    if (i < 8) {
                        rSetup.tex_gen_post[i] = static_cast< u8 >(J3D_PTIDENTITY);
                    }
                    if (i < 8) {
                        places[i] = static_cast< u8 >(texMtx);
                    }
                }
            }

            if (setTexMtx) {
                const TexSRT* const pTexSrt = GetTexSRTAry();

                for (u8 i = 0; i < mNum.texSRT; i++) {
                    if (!useTexMtx[i]) {
                        continue;
                    }

                    math::MTX34 mtx;
                    CalcTextureMtx(&mtx, pTexSrt[i]);
                    J3DNativeShape::setTexture((GetTexMtx(i) - J3D_TEXMTX0) / 3, mtx, 2);
                }
            }

            if (mNum.texMap > 0) {
                const TexMap* const pTexMap = GetTexMapAry();

                for (int i = 0; i < mNum.texMap && i < 8; i++) {
                    J3DNativeTexObj texObj;
                    pTexMap[i].Get(&texObj);
                    NebulaMaterialTexture& rTex = rValues.texture[i];
                    const u8 minFilter = texObj.mMinFilter;
                    rTex.image = reinterpret_cast< uintptr_t >(texObj.mImage);
                    rTex.width = texObj.mWidth;
                    rTex.height = texObj.mHeight;
                    rTex.wrap_s = texObj.mWrapS;
                    rTex.wrap_t = texObj.mWrapT;
                    rTex.min_linear = minFilter == 1 || minFilter == 3 || minFilter == 5;
                    rTex.mag_linear = texObj.mMagFilter != NEBULA_FILTER_NEAREST;
                    rTex.mip_filter = minFilter < 2 ? 0 : minFilter < 4 ? 1 : 2;
                    rTex.edge_lod = texObj.mEdgeLod;
                    rTex.anisotropy = texObj.mMaxAniso;
                    rTex.min_lod = texObj.mMinLod;
                    rTex.max_lod = texObj.mMaxLod;
                    rTex.lod_bias = texObj.mLodBias;
                }
            }

            for (u32 r = 0; r < 3; r++) {
                rValues.register_color[r][0] = mRegisterColors[r].r;
                rValues.register_color[r][1] = mRegisterColors[r].g;
                rValues.register_color[r][2] = mRegisterColors[r].b;
                rValues.register_color[r][3] = mRegisterColors[r].a;
            }
            for (u32 k = 0; k < 4; k++) {
                rValues.konst_color[k][0] = mConstantColors[k].r;
                rValues.konst_color[k][1] = mConstantColors[k].g;
                rValues.konst_color[k][2] = mConstantColors[k].b;
                rValues.konst_color[k][3] = mConstantColors[k].a;
            }

            // Each coordinate's scale: the size of the texture the cook found it samples.
            for (u32 coord = 0; coord < 8; coord++) {
                const u32 map = mCoordMap[coord];
                if (map < 8 && rValues.texture[map].image != 0) {
                    rValues.tex_coord_scale[coord].scale_s = rValues.texture[map].width;
                    rValues.tex_coord_scale[coord].scale_t = rValues.texture[map].height;
                }
            }
            if (mHasStages || mNum.texMap == 0) {
                useRasStage = true;
            }

            {
                const TexSRT* const pIndTexSrt = GetIndTexSRTAry();

                for (int i = 0; i < mNum.indSRT && i < 3; i++) {
                    f32 mtx[2][3];
                    CalcIndTexMtx(mtx, pIndTexSrt[i]);
                    for (u32 row = 0; row < 2; row++) {
                        for (u32 c = 0; c < 3; c++) {
                            rValues.ind_matrix[i].m[row][c] = mtx[row][c];
                        }
                    }
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
                rSetup.alpha_compare.comp0 = NEBULA_COMPARE_ALWAYS;
                rSetup.alpha_compare.op = J3D_AOP_AND;
                rSetup.alpha_compare.comp1 = NEBULA_COMPARE_ALWAYS;
            }

            if (IsBlendModeCap()) {
                const BlendMode* const pBlendMode = GetBlendModePtr();

                {
                    const u8 type = pBlendMode->GetType();
                    rSetup.blend.type = type == J3D_BM_BLEND ? NEBULA_BLEND_ADD : type == J3D_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
                                          : type == J3D_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
                }
                {
                    // NEBULA_FACTOR_*: source colour factors 2 and 3 are the destination's colour.
                    const u8 src = pBlendMode->GetSrcFactor();
                    rSetup.blend.source = src == J3D_BL_DSTCLR ? NEBULA_FACTOR_DST_COLOR
                                            : src == J3D_BL_INVDSTCLR ? NEBULA_FACTOR_INV_DST_COLOR : src;
                }
                rSetup.blend.destination = pBlendMode->GetDstFactor();
                rSetup.blend.logic_op = pBlendMode->GetOp();
            } else {
                rSetup.blend.type = NEBULA_BLEND_ADD;
                rSetup.blend.source = NEBULA_FACTOR_SRC_ALPHA;
                rSetup.blend.destination = NEBULA_FACTOR_INV_SRC_ALPHA;
                rSetup.blend.logic_op = J3D_LO_SET;
            }

            const bool remap = mRegisterColors[0] != DefaultBlackColor || !IsWhiteColor(mRegisterColors[1]);
            const u32 shader = mNativeShaders[(useRasStage ? 1 : 0) | (remap ? 2 : 0)];
            J3DNativeMaterial::sendWhole(shader != 0 ? NEBULA_SHADER_J3D_FIRST + shader - 1 : 0, rSetup, rValues);
            J3DNativeShape::setCurrentFromFile(0, places);

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
