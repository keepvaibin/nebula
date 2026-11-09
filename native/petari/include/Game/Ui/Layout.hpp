#pragma once

#include "Game/Ui/UiData.hpp"
#include "Game/Ui/Anchor.hpp"
#include "Game/Ui/Animation.hpp"
#include "Game/Ui/Group.hpp"
#include "Game/Ui/UiTypes.hpp"
#include <cstddef>
#include <new>

namespace Ui {
    struct Rect;

    template < typename C >
    class TagProcessorBase;

    namespace res {
        struct AnimShareInfoList;
    };

    class ResourceAccessor;
    struct ResBlockSet;
    class Pane;
    class GroupContainer;
    class DrawInfo;

    class Layout;

    enum OriginType {
        ORIGINTYPE_TOPLEFT,
        ORIGINTYPE_CENTER,

    };

    typedef UiList< AnimTransform > AnimTransformList;

    class Layout {
    public:
        Layout();
        // It owns its panes' memory: one layout, never copied.
        Layout(const Layout&) = delete;
        Layout& operator=(const Layout&) = delete;

        // From the game's current heap.
        static void* AllocMemory(u32 size) { return new u8[size]; }

        static void FreeMemory(void* mem) { delete[] static_cast< u8* >(mem); }

        virtual ~Layout();
        virtual bool Build(const void*, ResourceAccessor*);
        virtual AnimTransform* CreateAnimTransform();
        virtual AnimTransform* CreateAnimTransform(const void*, ResourceAccessor*);
        virtual AnimTransform* CreateAnimTransform(const AnimResource&, ResourceAccessor*);
        virtual void BindAnimation(AnimTransform*);
        virtual void UnbindAnimation(AnimTransform*);
        virtual void UnbindAllAnimation();
        virtual void SetAnimationEnable(AnimTransform*, bool);
        virtual void CalculateMtx(const DrawInfo&);
        virtual void Draw(const DrawInfo&);
        virtual void Animate(u32);
        virtual void SetTagProcessor(TagProcessorBase< char16_t >*);

        const Rect GetLayoutRect() const;

        // A pane from a cooked record, in this layout (a kind it does not know gives null).
        Pane* BuildPaneObj(const UiPaneData*, const ResBlockSet&);

        // The layout's memory: every pane and material made for it is constructed in its arena, each kind
        // joined to it from birth (the constructors take the layout), and destroyed at ~Layout, newest first,
        // whether it is in the tree or was removed from it. Nothing frees one of them alone.
        template < typename T, typename... Args >
        T* NewPane(Args&&... args) {
            return NewInArena< T >(*this, static_cast< Args&& >(args)...);
        }

        template < typename T, typename... Args >
        T* NewInArena(Args&&... args) {
            ArenaObject* const pObject = static_cast< ArenaObject* >(ArenaAlloc(sizeof(ArenaObject) + sizeof(T)));
            T* const pValue = new (pObject + 1) T(static_cast< Args&& >(args)...);
            // Linked once built: what its constructor made for it is destroyed after it.
            pObject->mDestroy = &DestroyInArena< T >;
            pObject->mNext = mArenaObjects;
            mArenaObjects = pObject;
            return pValue;
        }

        // Plain memory from the arena (16-byte aligned), freed with it.
        void* ArenaAlloc(u32 size);

        // The tree as it is now, depth first from the root: mPanes[i] is the i-th pane, mParent[i] its
        // parent's place (cNoParent at the root), mEnd[i] one past its branch (the next sibling of j is at
        // mEnd[j]). Built from the child links whenever an edit or rename moved the structure version.
        struct Order {
            static constexpr u32 cNoParent = 0xFFFFFFFFu;
            Pane** mPanes;
            u32* mParent;
            u32* mEnd;
            u32 mNum;
        };
        const Order& GetOrder() const;
        // Its place in GetOrder(); Pane::cNoListIndex when it is not in this layout's tree.
        u32 GetOrderIndex(const Pane* pPane) const;
        // The first pane in tree order whose name matches pName's first 16 characters (detail::EqualsResName),
        // through a name index built with the order; null when none.
        Pane* FindPane(const char* pName) const;

        // Every pane made in this layout has an id below this, kept for the layout's life (removed ones too).
        u32 GetPaneNum() const { return mPaneNum; }
        // A pane being constructed takes the next id (Pane's constructors call it).
        void Join(Pane* pPane);
        // The tree or a name changed: the order and the name index are rebuilt before the next use.
        void TouchStructure() { mStructureVersion++; }
        // The pane and its branch through the draw pass (a branch drawn on its own: SysInfoWindow's dim).
        void DrawBranch(const Pane* pPane, const DrawInfo& rInfo);

        // The canvas for the next passes (set before each) and the anchors CalculateMtx applies after the
        // matrices, in their order; the array stays the caller's.
        void SetCanvas(const Canvas& rCanvas) { mCanvas = rCanvas; }
        void SetAnchors(const Anchor* pAnchors, u32 num) {
            mpAnchors = pAnchors;
            mAnchorNum = num;
        }

        template < typename T >
        static T* NewObj();

        template < typename T, typename Param1 >
        static T* NewObj(Param1 param1);

        template < typename T, typename Param1, typename Param2 >
        static T* NewObj(Param1 param1, Param2 param2);

        template < typename T >
        static void DeleteArray(T objAry[], u32 num);

        template < typename T >
        static void DeletePrimArray(T objAry[]);

        AnimTransformList mAnimTransList;
        Pane* mpRootPane;
        GroupContainer* mpGroupContainer;
        Size mLayoutSize;
        // Where the layout's panes are measured from (ORIGINTYPE_*).
        u8 mOriginType;
        // The owning actor's life (LayoutActor::draw): every pane's draws carry it, so a layout killed and brought back
        // elsewhere starts at its new place on generated frames instead of sliding there from the old one.
        u32 mDrawGeneration;

    private:
        struct ArenaObject {
            ArenaObject* mNext;
            void (*mDestroy)(void*);
        };
        struct ArenaBlock;

        template < typename T >
        static void DestroyInArena(void* pValue) {
            static_cast< T* >(pValue)->~T();
        }

        void RefreshOrder() const;
        void FreeRetired() const;
        void DrawRange(u32 first, u32 end, const DrawInfo& rInfo);
        void ApplyAnchors(const Order& rOrder);
        void ApplyAnchor(const Order& rOrder, const Anchor& rAnchor, u32 place);
        // Moves the screen matrices of the drawn part of a branch.
        void TranslateBranch(const Order& rOrder, u32 place, f32 x, f32 y);

        const Anchor* mpAnchors = nullptr;
        u32 mAnchorNum = 0;
        // The root's inputs (the view matrix and whether the draw multiplies it in) as of the last pass, and
        // their version: the root's world rebuilds when it moves.
        Mtx34f mLastViewMtx;
        u32 mInputVersion = 0;
        bool mLastMultipleView = false;
        Canvas mCanvas = {};
        // The arena's blocks (newest first) and its objects (newest first).
        ArenaBlock* mArenaBlocks = nullptr;
        ArenaObject* mArenaObjects = nullptr;
        // The panes by id.
        Pane** mPaneTable = nullptr;
        u32 mPaneNum = 0;
        u32 mPaneCapacity = 0;
        u32 mStructureVersion = 1;
        // The order (and its scratch: the alpha context each place gives its children), the version it was
        // built at, and the name index over it (a slot holds an order place plus one, 0 empty; at most half
        // full; the first pane of each name in tree order).
        mutable Order mOrder = {};
        mutable f32* mContextAlpha = nullptr;
        mutable bool* mContextInfluenced = nullptr;
        mutable u32* mNameSlots = nullptr;
        mutable u32 mNameSlotMask = 0;
        mutable u32 mOrderVersion = 0;
        // Arrays an earlier rebuild replaced, freed when no pass can be walking them (no pass running).
        mutable void* mRetired = nullptr;
        mutable u32 mPassDepth = 0;
    };

    template < typename T >
    inline T* Layout::NewObj() {
        if (void* pMem = Layout::AllocMemory(sizeof(T))) {
            return new (pMem) T();
        }

        return 0;
    }

    template < typename T, typename Param1 >
    inline T* Layout::NewObj(Param1 param1) {
        if (void* pMem = Layout::AllocMemory(sizeof(T))) {
            return new (pMem) T(param1);
        }
        return 0;
    }

    template < typename T, typename Param1, typename Param2 >
    inline T* Layout::NewObj(Param1 param1, Param2 param2) {
        if (void* pMem = Layout::AllocMemory(sizeof(T))) {
            return new (pMem) T(param1, param2);
        }
        return 0;
    }

    template < typename T >
    inline void Layout::DeleteArray(T objAry[], u32 num) {
        if (objAry) {
            for (u32 i = 0; i < num; ++i) {
                objAry[i].~T();
            }
            FreeMemory(objAry);
        }
    }

    template < typename T >
    inline void Layout::DeletePrimArray(T objAry[]) {
        if (objAry) {
            FreeMemory(objAry);
        }
    }
}  // namespace Ui
