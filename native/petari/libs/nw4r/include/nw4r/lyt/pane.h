#pragma once

#include "Game/Ui/UiData.hpp"
#include "nw4r/lyt/types.h"
#include "nw4r/math/types.h"
#include "nw4r/ut/Color.h"
#include <cstddef>

namespace nw4r {
    namespace ut {
        struct Rect;
    };

    namespace lyt {
        class AnimTransform;
        class AnimationLink;
        class AnimResource;
        class Material;
        class DrawInfo;
        class Pane;
        struct ResBlockSet;

        typedef UiList< Pane > PaneList;

        class Pane {
        public:
            static constexpr UiPaneKind cKind = UiPaneKind_Pane;

            Pane();
            Pane(const UiPaneData*, const ResBlockSet&);
            virtual ~Pane();
            virtual void CalculateMtx(const DrawInfo&);
            virtual void Draw(const DrawInfo&);
            virtual void DrawSelf(const DrawInfo&);
            virtual void Animate(u32);
            virtual void AnimateSelf(u32);
            virtual const ut::Color GetVtxColor(u32) const;
            virtual void SetVtxColor(u32, ut::Color);
            virtual u8 GetColorElement(u32) const;
            virtual void SetColorElement(u32, u8);
            virtual u8 GetVtxColorElement(u32) const;
            virtual void SetVtxColorElement(u32, u8);
            virtual Pane* FindPaneByName(const char*, bool);
            virtual Material* FindMaterialByName(const char*, bool);
            virtual void BindAnimation(AnimTransform*, bool);
            virtual void UnbindAnimation(AnimTransform*, bool);
            virtual void UnbindAllAnimation(bool);
            virtual void UnbindAnimationSelf(AnimTransform*);
            virtual AnimationLink* FindAnimationLinkSelf(AnimTransform*);
            virtual void SetAnimationEnable(AnimTransform*, bool, bool);
            virtual Material* GetMaterial() const;
            virtual void LoadMtx(const DrawInfo&);

            void Init();
            void SetName(const char*);
            void SetUserData(const char*);

            bool IsUserAllocated() const {
                return mbUserAllocated;
            }

            bool IsInfluencedAlpha() const {
                return detail::TestBit(mFlag, 1);
            }

            bool IsLocationAdjust() const {
                return detail::TestBit(mFlag, 2);
            }

            Pane* GetParent() const {
                return mpParent;
            }

            void RemoveChild(Pane*);
            void AppendChild(Pane*);
            void AddAnimationLink(AnimationLink*);
            const math::VEC2 GetVtxPos() const;
            void CalculateMtxChild(const DrawInfo& rInfo);

            PaneList& GetChildList() {
                return mChildList;
            }

            const PaneList& GetChildList() const {
                return mChildList;
            }

            void SetInfluencedAlpha(bool influenced) {
                detail::SetBit(&mFlag, 1, influenced);
            }

            void SetSRTElement(u32 idx, f32 value) {
                f32* srtAry = &mTranslate.x;
                srtAry[idx] = value;
            }

            void SetVisible(bool visible) {
                detail::SetBit(&mFlag, 0, visible);
            }

            bool IsVisible() const {
                return detail::TestBit(mFlag, 0);
            }

            UiLink< Pane > mLink;  // in the parent's mChildList
            Pane* mpParent;
            PaneList mChildList;
            AnimationList mAnimList;
            Material* mpMaterial;
            math::VEC3 mTranslate;
            math::VEC3 mRotate;
            math::VEC2 mScale;
            Size mSize;
            math::MTX34 mMtx;
            math::MTX34 mGlbMtx;
            u8 mAlpha;
            u8 mGlbAlpha;
            u8 mBasePosition;
            u8 mFlag;
            UiPaneKind mKind;  // what the pane is (PaneCast); from its record, or the class that made it in code
            char mName[17];
            char mUserData[9];
            u8 mbUserAllocated;
            mutable u32 mDrawSlot = 0;
            // Its entry in its LayoutManager's pane list (LayoutManager::buildPaneInfo); cNoListIndex until listed.
            static constexpr u32 cNoListIndex = 0xFFFFFFFFu;
            u32 mListIndex = cNoListIndex;
            u8 mPadding;
        };
        // The pane as a T (Picture, TextBox, Window, Bounding) when it is one, else null; null stays null.
        template < class T >
        inline T* PaneCast(Pane* pPane) {
            return pPane != nullptr && pPane->mKind == T::cKind ? static_cast< T* >(pPane) : nullptr;
        }

        template < class T >
        inline const T* PaneCast(const Pane* pPane) {
            return pPane != nullptr && pPane->mKind == T::cKind ? static_cast< const T* >(pPane) : nullptr;
        }
    };  // namespace lyt
};  // namespace nw4r
