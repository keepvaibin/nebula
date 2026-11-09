#pragma once

#include "Game/Ui/UiData.hpp"
#include "Game/Ui/UiTypes.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Color.hpp"
#include <cstddef>

namespace Ui {
    struct Rect;

    class AnimTransform;
    class AnimationLink;
    class AnimResource;
    class Material;
    class DrawInfo;
    class Layout;
    class Pane;
    struct ResBlockSet;

    typedef UiList< Pane > PaneList;

    // What moves a pane each pass after its own transform and before its children's (a screen's control that
    // keeps a pane on a point): the world it gives is the one its children inherit.
    class PaneFollow {
    public:
        virtual void Follow(const Pane& rPane, Mtx34f& rWorld) const = 0;
    };

    class Pane {
    public:
        static constexpr UiPaneKind cKind = UiPaneKind_Pane;

        static constexpr u32 cNoId = 0xFFFFFFFFu;

        // Made by Layout::NewPane only: the pane is the layout's from birth and dies with it.
        explicit Pane(Layout&);
        Pane(Layout&, const UiPaneData*, const ResBlockSet&);
        virtual ~Pane();
        // The pane's local, world and screen matrices and global alpha from its parent's world and the alpha
        // its ancestors pass down (Layout::CalculateMtx runs it over the tree order, then the anchors); returns
        // the alpha context its children get. Each matrix is rebuilt only when what it is built from changed:
        // the local from the transform (compared bit for bit), the world from the local and the parent's world
        // version (rootVersion at the root: the layout's view inputs) or a follow, the screen from the world,
        // the screen scale, or every pass while anchors write it (resetScreen).
        void CalculateMtxSelf(const DrawInfo&, f32& rContextAlpha, bool& rContextInfluenced, u32 rootVersion, bool resetScreen);
        // This pane and its branch, as the layout's draw pass draws them (a branch drawn on its own).
        void Draw(const DrawInfo&);
        virtual void DrawSelf(const DrawInfo&);
        virtual void AnimateSelf(u32);
        virtual const Color8 GetVtxColor(u32) const;
        virtual void SetVtxColor(u32, Color8);
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

        bool IsInfluencedAlpha() const {
            return detail::TestBit(mFlag, 1);
        }

        bool IsLocationAdjust() const {
            return detail::TestBit(mFlag, 2);
        }

        Pane* GetParent() const {
            return mpParent;
        }

        // Tree edits, between panes of one layout; the layout's order follows every edit and rename by itself.
        // A removed pane stays the layout's (it can join again) until the layout ends.
        void RemoveChild(Pane*);
        void AppendChild(Pane*);
        void AddAnimationLink(AnimationLink*);
        const Vec2f GetVtxPos() const;

        Layout* GetLayout() const {
            return mpLayout;
        }

        // The size the pane is drawn and pointed at: an anchor's (a backing, a cinema bar, a framed window), or
        // the authored or animated size.
        const Size& GetDrawSize() const {
            return mHasDrawSize ? mDrawSize : mSize;
        }

        // The pane's slot in its layout (Layout::GetPaneNum bounds it), for state kept beside the layout.
        u32 GetId() const {
            return mId;
        }

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
        Vec3f mTranslate;
        Vec3f mRotate;
        Vec2f mScale;
        Size mSize;
        Mtx34f mMtx;
        // On screen: the world with the pane's own screen scale and its anchors (what it is drawn with).
        Mtx34f mGlbMtx;
        // The parent's world times the local matrix, then the follow: what the children inherit.
        Mtx34f mWorldMtx;
        u8 mAlpha;
        u8 mGlbAlpha;
        u8 mBasePosition;
        u8 mFlag;
        UiPaneKind mKind;  // what the pane is (PaneCast); from its record, or the class that made it in code
        NameText< 17 > mName;
        char mUserData[9];  // the layout file's 8-byte user data field
        mutable u32 mDrawSlot = 0;
        // Its place in its layout's tree order (Layout::GetOrder, which refreshes it); cNoListIndex outside it.
        static constexpr u32 cNoListIndex = 0xFFFFFFFFu;
        u32 mListIndex = cNoListIndex;
        Layout* mpLayout = nullptr;
        u32 mId = cNoId;
        const PaneFollow* mpFollow = nullptr;
        // Scales the pane's own screen matrix about its origin (its children keep the unscaled world).
        f32 mScreenScale = 1.0f;
        Size mDrawSize;
        bool mHasDrawSize = false;
        // What the matrices were last built from (CalculateMtxSelf).
        Vec3f mBuiltTranslate;
        Vec3f mBuiltRotate;
        Vec2f mBuiltScale;  // with the location adjust
        u32 mWorldVersion = 0;  // moves whenever the world changes
        u32 mBuiltParentVersion = cNoVersion;
        f32 mBuiltScreenScale = 1.0f;
        bool mLocalBuilt = false;
        static constexpr u32 cNoVersion = 0xFFFFFFFFu;
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
}  // namespace Ui
