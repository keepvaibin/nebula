#pragma once

#include "nw4r/lyt/animation.h"
#include "nw4r/lyt/texMap.h"
#include "nw4r/lyt/types.h"
#include "nw4r/ut/Color.h"

namespace nw4r {
    namespace lyt {
        class TexMap;

        namespace detail {
            struct StorageNums {
                u32 texMap : 4;
                u32 texSRT : 4;
                u32 texCoordGen : 4;
                u32 indSRT : 2;
                u32 chanCtrl : 1;
                u32 matCol : 1;
                u32 alpComp : 1;
                u32 blendMode : 1;
            };
        };  // namespace detail
        class Material {
        public:
            Material();
            Material(const res::Material*, const ResBlockSet&);

            const char* GetName() const {
                return mName;
            }

            bool IsUserAllocated() const {
                return mbUserAllocated;
            }

            virtual ~Material();
            virtual bool Setup(bool, u8);
            virtual void BindAnimation(AnimTransform*);
            virtual void UnbindAnimation(AnimTransform*);
            virtual void UnbindAllAnimation();
            virtual void Animate();
            virtual AnimationLink* FindAnimationLink(AnimTransform*);
            virtual void SetAnimationEnable(AnimTransform*, bool);

            void Init();
            void InitStorageNums(detail::StorageNums*);
            void ReserveStorage(u8 texMapNum, u8 texSRTNum, u8 texCoordGenNum, u8 indSRTNum,
                              bool allocChanCtrl, bool allocMatCol, bool allocAlpComp, bool allocBlendMode);

            u8 GetTexSRTCap() const {
                return u8(mCap.texSRT);
            }

            u8 GetIndTexSRTCap() const {
                return u8(mCap.indSRT);
            }
            TexMap* GetTexturePtr(u8 idx) {
                return &GetTexMapAry()[idx];
            }

            void SetTexSRTElement(u32 idx, u32 element, f32 value) {
                f32* const srtAry = &GetTexSRTAry()[idx].translate.x;
                srtAry[element] = value;
            }

            void SetIndTexSRTElement(u32 idx, u32 element, f32 value) {
                f32* const srtAry = &GetIndTexSRTAry()[idx].translate.x;
                srtAry[element] = value;
            }

            u8 GetTextureNum() const {
                return u8(mNum.texMap);
            }

            void SetTextureNum(u8 num);

            u8 GetTextureCap() const {
                return u8(mCap.texMap);
            }

            u8 GetTexCoordGenCap() const {
                return u8(mCap.texCoordGen);
            }

            void SetTexCoordGenNum(u8 num);
            void SetTexture(u8 texMapIdx, const TexMap& texMap) {
                GetTexMapAry()[texMapIdx].Set(texMap);
            }

            void SetTexCoordGen(u32 idx, TexCoordGen value) {
                GetTexCoordGenAry()[idx] = value;
            }

            bool IsAlphaCompareCap() const {
                return mCap.alpComp;
            }

            bool IsChanCtrlCap() const {
                return mCap.chanCtrl;
            }

            bool IsMatColorCap() const {
                return mCap.matCol;
            }

            bool IsBlendModeCap() const {
                return mCap.blendMode;
            }

            const TexMap& GetTexture(u8 texMapIdx) const {
                return GetTexMapAry()[texMapIdx];
            }

            void SetStageColor(u32 idx, const NebulaColor16& color) {
                mRegisterColors[idx] = color;
            }

            void SetStageConstant(u32 idx, ut::Color value) {
                mConstantColors[idx] = value;
            }

            void SetTexSRT(u32 texSRTIdx, const TexSRT& value) {
                GetTexSRTAry()[texSRTIdx] = value;
            }

            void SetIndTexSRT(u32 texSRTIdx, const TexSRT& value) {
                GetIndTexSRTAry()[texSRTIdx] = value;
            }

            void SetChanCtrl(ChanCtrl value) {
                GetChanCtrlAry()[0] = value;
            }

            void SetMatColor(ut::Color value) {
                GetMatColAry()[0] = value;
            }

            void SetAlphaCompare(AlphaCompare value) {
                *GetAlphaComparePtr() = value;
            }

            void SetBlendMode(BlendMode value) {
                *GetBlendModePtr() = value;
            }

            const NebulaColor16 GetStageColor(u32 idx) const {
                return mRegisterColors[idx];
            }

            const TexMap* GetTexMapAry() const;
            TexMap* GetTexMapAry();
            const TexCoordGen* GetTexCoordGenAry() const;
            TexCoordGen* GetTexCoordGenAry();
            TexSRT* GetTexSRTAry();
            ChanCtrl* GetChanCtrlAry();
            ut::Color* GetMatColAry();
            AlphaCompare* GetAlphaComparePtr();
            BlendMode* GetBlendModePtr();
            TexSRT* GetIndTexSRTAry();
            void SetName(const char*);
            void SetColorElement(u32, s16);
            void AddAnimationLink(AnimationLink*);

            AnimationList mAnimList;
            NebulaColor16 mRegisterColors[3];
            ut::Color mConstantColors[4];
            detail::StorageNums mCap;
            detail::StorageNums mNum;
            void* mpStorage;
            char mName[21];
            u8 mbUserAllocated;
            u32 mNativeShaders[4];
            u8 mCoordMap[8];  // each texture coordinate's texture (the cook's; its size scales the coordinate)
            bool mHasStages;  // the shader combines more than the default of a material without its own
        };
    };  // namespace lyt
};  // namespace nw4r
