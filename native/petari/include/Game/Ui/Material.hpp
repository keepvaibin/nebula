#pragma once

#include "Game/Ui/Animation.hpp"
#include "Game/Ui/TexMap.hpp"
#include "Game/Ui/UiTypes.hpp"
#include "Game/Util/Color.hpp"

namespace Ui {
    class TexMap;

    namespace detail {
        // How many of each a material holds (or has room for): the lists are counted, the single records 0 or 1.
        struct StorageNums {
            u32 texMap;
            u32 texSRT;
            u32 texCoordGen;
            u32 indSRT;
            u32 chanCtrl;
            u32 matCol;
            u32 alpComp;
            u32 blendMode;
        };
    };  // namespace detail
    class Material {
    public:
        Material();
        Material(const UiMaterialData*, const ResBlockSet&);

        const char* GetName() const {
            return mName;
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
        void ReserveStorage(u32 texMapNum, u32 texSRTNum, u32 texCoordGenNum, u32 indSRTNum,
                          bool allocChanCtrl, bool allocMatCol, bool allocAlpComp, bool allocBlendMode);

        u32 GetTexSRTCap() const {
            return mCap.texSRT;
        }

        u32 GetIndTexSRTCap() const {
            return mCap.indSRT;
        }
        TexMap* GetTexturePtr(u32 idx) {
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

        u32 GetTextureNum() const {
            return mNum.texMap;
        }

        void SetTextureNum(u32 num);

        u32 GetTextureCap() const {
            return mCap.texMap;
        }

        u32 GetTexCoordGenCap() const {
            return mCap.texCoordGen;
        }

        void SetTexCoordGenNum(u32 num);
        void SetTexture(u32 texMapIdx, const TexMap& texMap) {
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

        const TexMap& GetTexture(u32 texMapIdx) const {
            return GetTexMapAry()[texMapIdx];
        }

        void SetStageColor(u32 idx, const NebulaColor16& color) {
            mRegisterColors[idx] = color;
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
        Color8* GetMatColAry();
        AlphaCompare* GetAlphaComparePtr();
        BlendMode* GetBlendModePtr();
        TexSRT* GetIndTexSRTAry();
        void SetName(const char*);
        void SetColorElement(u32, s16);
        void AddAnimationLink(AnimationLink*);

        AnimationList mAnimList;
        NebulaColor16 mRegisterColors[3];
        Color8 mConstantColors[4];
        detail::StorageNums mCap;
        detail::StorageNums mNum;
        void* mpStorage;
        NameText< 21 > mName;
        const UiLayoutHeader* mpFile;    // the cooked layout the material was read from: its forms' tables lie in it
        const UiFormData* mNativeForms;  // the cook's four forms in the layout file
        const u8* mCoordMap;  // each texture coordinate's texture, 255 none (the cook's; its size scales the coordinate)
        u32 mCoordCount;
        bool mHasStages;  // the shader combines more than the default of a material without its own
    };
}  // namespace Ui
