#include "Game/Ui/Animation.hpp"
#include "Game/Ui/Bounding.hpp"
#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/DrawInfo.hpp"
#include "Game/Ui/Group.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Material.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/Picture.hpp"
#include "Game/Ui/TextBox.hpp"
#include "Game/Ui/UiUtil.hpp"
#include "Game/Ui/Window.hpp"
#include "Game/Ui/Rect.hpp"
#include <nebula_module.h>
#include "Game/Util/MotionSignal.hpp"
#include <cstring>

namespace {
    using namespace Ui;
    void SetTagProcessorImpl(Pane* pPane, Ui::TagProcessorBase< char16_t >* pTagProcessor) {
        if (TextBox* pTextBox = PaneCast< TextBox >(pPane)) {
            pTextBox->SetTagProcessor(pTagProcessor);
        }

        for (Pane* pChild : pPane->GetChildList()) {
            SetTagProcessorImpl(pChild, pTagProcessor);
        }
    }

    // One rebuild's arrays in one block, after this header: the panes, the alpha context, the name slots, the
    // parent and end places, the influenced flags (largest alignment first). A replaced block waits in a chain
    // while a pass may still walk it.
    struct OrderBlock {
        OrderBlock* mNext;
    };

    // FNV-1a over a pane name.
    u32 HashPaneName(const char* pName) {
        return uiNameHash(pName);
    }

    u32 CountBranch(const Pane* pPane) {
        u32 count = 1;
        for (const Pane* pChild : pPane->GetChildList()) {
            count += CountBranch(pChild);
        }
        return count;
    }

    void PlaceBranch(Pane* pPane, u32 parent, Layout::Order& rOrder, u32& rNum) {
        const u32 index = rNum++;
        rOrder.mPanes[index] = pPane;
        rOrder.mParent[index] = parent;
        pPane->mListIndex = index;
        for (Pane* pChild : pPane->GetChildList()) {
            PlaceBranch(pChild, index, rOrder, rNum);
        }
        rOrder.mEnd[index] = rNum;
    }

    // The arena's block sizes: the first, and the most a doubling reaches.
    constexpr u32 cArenaBlockFirst = 4096;
    constexpr u32 cArenaBlockMax = 65536;
    constexpr u32 cArenaAlign = 16;

    void FreeBlocks(OrderBlock* pBlock) {
        while (pBlock != nullptr) {
            OrderBlock* const pNext = pBlock->mNext;
            Layout::FreeMemory(pBlock);
            pBlock = pNext;
        }
    }
}  // namespace

namespace Ui {
    struct Layout::ArenaBlock {
        ArenaBlock* mNext;
        u32 mUsed;
        u32 mSize;
        // The block's bytes follow, from a cArenaAlign boundary.
    };

    Layout::Layout() : mpRootPane(nullptr), mpGroupContainer(nullptr), mLayoutSize(0.0f, 0.0f), mOriginType(0), mDrawGeneration(0) {
    }

    // Every arena object newest first (a pane before the materials its constructor made; the groups, built last,
    // before the panes they name), then the animations the panes were bound to, then the memory.
    Layout::~Layout() {
        for (ArenaObject* pObject = mArenaObjects; pObject != nullptr; pObject = pObject->mNext) {
            pObject->mDestroy(pObject + 1);
        }
        mArenaObjects = nullptr;
        mpRootPane = nullptr;
        mpGroupContainer = nullptr;

        for (AnimTransform *pAnimTrans = mAnimTransList.First(), *pNext; pAnimTrans != nullptr; pAnimTrans = pNext) {
            pNext = AnimTransformList::Next(pAnimTrans);
            mAnimTransList.Erase(pAnimTrans);
            pAnimTrans->~AnimTransform();
            FreeMemory(pAnimTrans);
        }

        delete[] mPaneTable;
        if (mOrder.mPanes != nullptr) {
            FreeBlocks(reinterpret_cast< OrderBlock* >(mOrder.mPanes) - 1);
        }
        FreeBlocks(static_cast< OrderBlock* >(mRetired));
        while (mArenaBlocks != nullptr) {
            ArenaBlock* const pNext = mArenaBlocks->mNext;
            FreeMemory(mArenaBlocks);
            mArenaBlocks = pNext;
        }
    }

    void* Layout::ArenaAlloc(u32 size) {
        const u32 need = (size + cArenaAlign - 1) & ~(cArenaAlign - 1);
        ArenaBlock* pBlock = mArenaBlocks;
        if (pBlock == nullptr || pBlock->mSize - pBlock->mUsed < need) {
            // Each block twice the last (a small layout holds little), to cArenaBlockMax; an object larger
            // than that gets a block of its own size.
            u32 bytes = pBlock == nullptr ? cArenaBlockFirst : pBlock->mSize * 2;
            bytes = bytes > cArenaBlockMax ? cArenaBlockMax : bytes;
            bytes = need > bytes ? need : bytes;
            // The header is padded to cArenaAlign so the block's bytes start aligned (the allocator returns
            // memory aligned for any type).
            const u32 head = (static_cast< u32 >(sizeof(ArenaBlock)) + cArenaAlign - 1) & ~(cArenaAlign - 1);
            pBlock = static_cast< ArenaBlock* >(AllocMemory(head + bytes));
            pBlock->mNext = mArenaBlocks;
            pBlock->mUsed = 0;
            pBlock->mSize = bytes;
            mArenaBlocks = pBlock;
        }
        const u32 head = (static_cast< u32 >(sizeof(ArenaBlock)) + cArenaAlign - 1) & ~(cArenaAlign - 1);
        void* const pMem = reinterpret_cast< u8* >(pBlock) + head + pBlock->mUsed;
        pBlock->mUsed += need;
        return pMem;
    }

    void Layout::Join(Pane* pPane) {
        if (mPaneNum == mPaneCapacity) {
            const u32 capacity = mPaneCapacity == 0 ? 64 : mPaneCapacity * 2;
            Pane** const pTable = new Pane*[capacity];
            for (u32 i = 0; i < mPaneNum; i++) {
                pTable[i] = mPaneTable[i];
            }
            delete[] mPaneTable;
            mPaneTable = pTable;
            mPaneCapacity = capacity;
        }
        pPane->mpLayout = this;
        pPane->mId = mPaneNum;
        mPaneTable[mPaneNum++] = pPane;
    }

    void Layout::FreeRetired() const {
        FreeBlocks(static_cast< OrderBlock* >(mRetired));
        mRetired = nullptr;
    }

    void Layout::RefreshOrder() const {
        if (mOrderVersion == mStructureVersion) {
            return;
        }

        const u32 num = mpRootPane != nullptr && mpRootPane->mpLayout == this ? CountBranch(mpRootPane) : 0;
        u32 slots = 2;
        while (slots < num * 2) {
            slots *= 2;
        }
        const u32 size = sizeof(OrderBlock) + num * (sizeof(Pane*) + sizeof(f32) + 2 * sizeof(u32) + sizeof(bool)) +
                         slots * sizeof(u32);
        OrderBlock* const pBlock = static_cast< OrderBlock* >(AllocMemory(size));
        pBlock->mNext = nullptr;
        u8* pCursor = reinterpret_cast< u8* >(pBlock + 1);
        Order order;
        order.mPanes = reinterpret_cast< Pane** >(pCursor);
        pCursor += num * sizeof(Pane*);
        f32* const pContextAlpha = reinterpret_cast< f32* >(pCursor);
        pCursor += num * sizeof(f32);
        u32* const pSlots = reinterpret_cast< u32* >(pCursor);
        pCursor += slots * sizeof(u32);
        order.mParent = reinterpret_cast< u32* >(pCursor);
        pCursor += num * sizeof(u32);
        order.mEnd = reinterpret_cast< u32* >(pCursor);
        pCursor += num * sizeof(u32);
        bool* const pContextInfluenced = reinterpret_cast< bool* >(pCursor);
        order.mNum = 0;
        if (num != 0) {
            PlaceBranch(mpRootPane, Order::cNoParent, order, order.mNum);
        }

        for (u32 i = 0; i < slots; i++) {
            pSlots[i] = 0;
        }
        const u32 mask = slots - 1;
        for (u32 i = 0; i < order.mNum; i++) {
            const char* const pName = order.mPanes[i]->mName;
            u32 slot = HashPaneName(pName) & mask;
            while (pSlots[slot] != 0 && !uiSameName(order.mPanes[pSlots[slot] - 1]->mName, pName)) {
                slot = (slot + 1) & mask;
            }
            if (pSlots[slot] == 0) {
                pSlots[slot] = i + 1;
            }
        }

        // The block it replaces: freed now, or kept until no pass can be walking it.
        if (mOrder.mPanes != nullptr) {
            OrderBlock* const pOld = reinterpret_cast< OrderBlock* >(mOrder.mPanes) - 1;
            if (mPassDepth == 0) {
                FreeBlocks(pOld);
            } else {
                pOld->mNext = static_cast< OrderBlock* >(mRetired);
                mRetired = pOld;
            }
        }
        mOrder = order;
        mContextAlpha = pContextAlpha;
        mContextInfluenced = pContextInfluenced;
        mNameSlots = pSlots;
        mNameSlotMask = mask;
        mOrderVersion = mStructureVersion;
    }

    const Layout::Order& Layout::GetOrder() const {
        RefreshOrder();
        return mOrder;
    }

    u32 Layout::GetOrderIndex(const Pane* pPane) const {
        RefreshOrder();
        const u32 index = pPane->mListIndex;
        return index < mOrder.mNum && mOrder.mPanes[index] == pPane ? index : Pane::cNoListIndex;
    }

    Pane* Layout::FindPane(const char* pName) const {
        RefreshOrder();
        for (u32 slot = HashPaneName(pName) & mNameSlotMask; mNameSlots[slot] != 0; slot = (slot + 1) & mNameSlotMask) {
            Pane* const pPane = mOrder.mPanes[mNameSlots[slot] - 1];
            if (uiSameName(pPane->mName, pName)) {
                return pPane;
            }
        }
        return nullptr;
    }

    // A cooked layout (UiData.hpp): panes in tree order, so each one's parent is built before it; groups name their
    // panes by index (the first of each name, as FindPaneByName from the root found them).
    bool Layout::Build(const void* lytResBuf, ResourceAccessor* pResAcsr) {
        const UiLayoutHeader* const pHead = static_cast< const UiLayoutHeader* >(lytResBuf);
        if (pHead->mMagic != cUiLayoutMagic || pHead->mVersion != cUiDataVersion) {
            if (pHead->mMagic == cUiLayoutMagic) {
                nebula_log("UiData: a layout of version %u; this game reads version %u (the pak needs a new cook)\n",
                        static_cast< unsigned >(pHead->mVersion), static_cast< unsigned >(cUiDataVersion));
            }
            return false;
        }

        mOriginType = (pHead->mFlags & UiLayoutFlag_Centered) != 0 ? ORIGINTYPE_CENTER : ORIGINTYPE_TOPLEFT;
        mLayoutSize = Size(pHead->mWidth, pHead->mHeight);

        Pane** const ppPanes = static_cast< Pane** >(AllocMemory(sizeof(Pane*) * (pHead->mPaneCount + 1)));
        // Ids in record order (tree order): the cooked panes are the first slots.
        const ResBlockSet resBlockSet = {pHead, pResAcsr, ppPanes};
        for (u32 i = 0; i < pHead->mPaneCount; ++i) {
            const UiPaneData* const pRes = resBlockSet.PaneAt(i);
            Pane* const pPane = BuildPaneObj(pRes, resBlockSet);
            ppPanes[i] = pPane;
            if (pPane == nullptr) {
                continue;
            }
            if (mpRootPane == nullptr) {
                mpRootPane = pPane;
            }
            if (pRes->mParent != cUiNone16 && ppPanes[pRes->mParent] != nullptr) {
                ppPanes[pRes->mParent]->AppendChild(pPane);
            }
        }

        if ((pHead->mFlags & UiLayoutFlag_GroupContainer) != 0) {
            mpGroupContainer = NewInArena< GroupContainer >();
            const UiGroupData* const pGroups = resBlockSet.At< UiGroupData >(pHead->mGroups);
            for (u32 i = 0; i < pHead->mGroupCount; ++i) {
                mpGroupContainer->AppendGroup(NewInArena< Group >(*this, &pGroups[i], resBlockSet));
            }
        }

        FreeMemory(ppPanes);
        TouchStructure();
        return true;
    }

    AnimTransform* Layout::CreateAnimTransform() {
        AnimTransformBasic* const pAnimTrans = NewObj< AnimTransformBasic >();
        if (pAnimTrans != nullptr) {
            mAnimTransList.PushBack(pAnimTrans);
        }
        return pAnimTrans;
    }

    AnimTransform* Layout::CreateAnimTransform(const void* animResBuf, ResourceAccessor* pResAcsr) {
        return CreateAnimTransform(AnimResource(animResBuf), pResAcsr);
    }

    AnimTransform* Layout::CreateAnimTransform(const AnimResource& animRes, ResourceAccessor* pResAcsr) {
        const UiAnimationHeader* const pAnimBlock = animRes.GetResourceBlock();

        if (pAnimBlock == nullptr) {
            return nullptr;
        }

        AnimTransform* const pAnimTrans = CreateAnimTransform();
        if (pAnimTrans != nullptr) {
            pAnimTrans->SetResource(pAnimBlock, pResAcsr);
        }
        return pAnimTrans;
    }

    void Layout::BindAnimation(AnimTransform* pAnimTrans) {
        if (mpRootPane != nullptr) {
            mpRootPane->BindAnimation(pAnimTrans, true);
        }
    }

    void Layout::UnbindAnimation(AnimTransform* pAnimTrans) {
        if (mpRootPane != nullptr) {
            mpRootPane->UnbindAnimation(pAnimTrans, true);
        }
    }

    void Layout::UnbindAllAnimation() {
        UnbindAnimation(0);
    }

    void Layout::SetAnimationEnable(AnimTransform* pAnimTrans, bool bEnable) {
        if (mpRootPane != nullptr) {
            mpRootPane->SetAnimationEnable(pAnimTrans, bEnable, true);
        }
    }

    // The passes walk the order (a copy of its pointers: a lookup inside the pass may rebuild it, and the block
    // it replaces stays until the pass ends). An invisible pane's branch is skipped by jumping to its end.
    void Layout::CalculateMtx(const DrawInfo& rInfo) {
        if (mPassDepth++ == 0) {
            FreeRetired();
        }
        RefreshOrder();
        const Order order = mOrder;
        f32* const pContextAlpha = mContextAlpha;
        bool* const pContextInfluenced = mContextInfluenced;
        const bool calculateInvisible = rInfo.IsInvisiblePaneCalculateMtx();
        const bool multipleView = rInfo.IsMultipleViewMtxOnDraw();
        bool inputsMoved = mInputVersion == 0 || multipleView != mLastMultipleView;
        for (u32 i = 0; !inputsMoved && i < 12; i++) {
            inputsMoved = __builtin_bit_cast(u32, (&rInfo.GetViewMtx().mMtx[0][0])[i]) != __builtin_bit_cast(u32, (&mLastViewMtx.mMtx[0][0])[i]);
        }
        if (inputsMoved) {
            mLastViewMtx = rInfo.GetViewMtx();
            mLastMultipleView = multipleView;
            mInputVersion = mInputVersion + 1 == Pane::cNoVersion ? 1 : mInputVersion + 1;
        }
        const bool resetScreen = mAnchorNum != 0;
        for (u32 i = 0; i < order.mNum;) {
            Pane* const pPane = order.mPanes[i];
            if (!pPane->IsVisible() && !calculateInvisible) {
                i = order.mEnd[i];
                continue;
            }
            const u32 parent = order.mParent[i];
            f32 alpha = parent == Order::cNoParent ? rInfo.GetGlobalAlpha() : pContextAlpha[parent];
            bool influenced = parent == Order::cNoParent ? rInfo.IsInfluencedAlpha() : pContextInfluenced[parent];
            pPane->CalculateMtxSelf(rInfo, alpha, influenced, mInputVersion, resetScreen);
            pContextAlpha[i] = alpha;
            pContextInfluenced[i] = influenced;
            i++;
        }
        if (mAnchorNum != 0) {
            ApplyAnchors(order);
        }
        mPassDepth--;
    }

    void Layout::TranslateBranch(const Order& rOrder, u32 place, f32 x, f32 y) {
        for (u32 i = place; i < rOrder.mEnd[place];) {
            Pane* const pPane = rOrder.mPanes[i];
            if (!pPane->IsVisible()) {
                i = rOrder.mEnd[i];
                continue;
            }
            pPane->mGlbMtx.mMtx[0][3] += x;
            pPane->mGlbMtx.mMtx[1][3] += y;
            i++;
        }
    }

    // An anchor applies to a pane drawn this pass (it and every ancestor visible), the sizes of the backings and
    // cinema bars to one on a wide canvas whatever it shows.
    void Layout::ApplyAnchors(const Order& rOrder) {
        const Canvas& rCanvas = mCanvas;
        const bool enlarged = rCanvas.mHorizontalDelta != 0.0f || rCanvas.mVerticalDelta != 0.0f;
        for (u32 n = 0; n < mAnchorNum; n++) {
            const Anchor& rAnchor = mpAnchors[n];
            Pane* const pPane = rAnchor.mpPane;
            const u32 place = pPane->mListIndex;
            bool drawn = place < rOrder.mNum && rOrder.mPanes[place] == pPane;
            for (u32 i = place; drawn && i != Order::cNoParent; i = rOrder.mParent[i]) {
                drawn = rOrder.mPanes[i]->IsVisible();
            }
            if ((rAnchor.mFit & (AnchorFill | AnchorCinemaUpper | AnchorCinemaLower)) != 0) {
                pPane->mHasDrawSize = rCanvas.mWide;
                if (!rCanvas.mWide) {
                    continue;
                }
                if ((rAnchor.mFit & AnchorFill) != 0) {
                    pPane->mDrawSize = Size(rCanvas.mWidth, rCanvas.mHeight);
                    continue;
                }
                // Wait, Appear and End author a 60-unit bar; Close and Open animate it to 250 to meet at the
                // centre, so a closing bar also gains its share of the extra half height.
                const f32 height = pPane->mSize.height;
                pPane->mDrawSize = Size(rCanvas.mWidth, height);
                if (rCanvas.mVerticalDelta == 0.0f || !(height > 0.0f)) {
                    continue;
                }
                f32 closing = (height - 60.0f) / 190.0f;
                closing = closing < 0.0f ? 0.0f : closing > 1.0f ? 1.0f : closing;
                pPane->mDrawSize.height = height + closing * rCanvas.mVerticalDelta;
                if (drawn) {
                    const bool upper = (rAnchor.mFit & AnchorCinemaUpper) != 0;
                    TranslateBranch(rOrder, place, 0.0f, upper ? rCanvas.mVerticalDelta : -rCanvas.mVerticalDelta);
                }
                continue;
            }
            if (!drawn || (!enlarged && !(rAnchor.mFit & (AnchorFrame | AnchorFlipSlide)))) {
                continue;
            }
            ApplyAnchor(rOrder, rAnchor, place);
        }
    }

    void Layout::ApplyAnchor(const Order& rOrder, const Anchor& rAnchor, u32 place) {
        const Canvas& rCanvas = mCanvas;
        Pane* const pPane = rAnchor.mpPane;
        Mtx34f& rScreen = pPane->mGlbMtx;
        // A scale multiplies a row of the screen matrix: the pane scales about the canvas centre.
        if (rAnchor.mFit & AnchorCover) {
            const f32 k = rCanvas.mScaleX > rCanvas.mScaleY ? rCanvas.mScaleX : rCanvas.mScaleY;
            for (s32 i = 0; i < 4; i++) {
                rScreen.mMtx[0][i] *= k;
                rScreen.mMtx[1][i] *= k;
            }
        }
        if (rAnchor.mFit & AnchorWidth) {
            for (s32 i = 0; i < 4; i++) {
                rScreen.mMtx[0][i] *= rCanvas.mScaleX;
            }
        }
        if (rAnchor.mFit & AnchorHeight) {
            for (s32 i = 0; i < 4; i++) {
                rScreen.mMtx[1][i] *= rCanvas.mScaleY;
            }
        }
        if (rAnchor.mFit & (AnchorExtendUp | AnchorExtendDown)) {
            // Grows a bar about its inner edge until its outer edge is the vertical delta further out. A picture
            // spans 0..-h, h/2..-h/2 or h..0 by its base position's row.
            const f32 delta = rCanvas.mVerticalDelta;
            const bool up = (rAnchor.mFit & AnchorExtendUp) != 0;
            const f32 height = pPane->mSize.height;
            const s32 row = pPane->mBasePosition / 3;
            const f32 top = row == 0 ? 0.0f : row == 1 ? height * 0.5f : height;
            const f32 m11 = rScreen.mMtx[1][1];
            const f32 m13 = rScreen.mMtx[1][3];
            const f32 a = m11 * top + m13;
            const f32 b = m11 * (top - height) + m13;
            const f32 span = __builtin_fabsf(a - b);
            if (span > 0.0f && delta != 0.0f) {
                const f32 inner = up ? (a < b ? a : b) : (a > b ? a : b);
                const f32 k = (span + delta) / span;
                rScreen.mMtx[1][0] *= k;
                rScreen.mMtx[1][1] *= k;
                rScreen.mMtx[1][2] *= k;
                rScreen.mMtx[1][3] = inner + (m13 - inner) * k;
            }
        }
        if ((rAnchor.mFit & AnchorFlipSlide) && pPane->mpParent != nullptr && pPane->mTranslate.y != 0.0f) {
            TranslateBranch(rOrder, place, 0.0f, -2.0f * pPane->mpParent->mGlbMtx.mMtx[1][1] * pPane->mTranslate.y);
        }
        if (rAnchor.mFit & AnchorFrame) {
            const f32 sx = __builtin_fabsf(rScreen.mMtx[0][0]);
            const f32 sy = __builtin_fabsf(rScreen.mMtx[1][1]);
            if (sx > 0.0f && sy > 0.0f) {
                pPane->mDrawSize = Size(rAnchor.mWidth + 2.0f * rCanvas.mHorizontalDelta / sx,
                                        rAnchor.mHeight + 2.0f * rCanvas.mVerticalDelta / sy);
                pPane->mHasDrawSize = true;
            }
        }
        f32 x = (rAnchor.mFit & AnchorLeft) ? -rCanvas.mHorizontalDelta : (rAnchor.mFit & AnchorRight) ? rCanvas.mHorizontalDelta : 0.0f;
        // A pane the game slides off the 16:9 canvas (local x reaching mWidth) must leave the wider one too.
        if (rAnchor.mFit & AnchorSlide) {
            x += pPane->mTranslate.x * rCanvas.mHorizontalDelta / rAnchor.mWidth;
        }
        const f32 y = (rAnchor.mFit & AnchorTop) ? rCanvas.mVerticalDelta : (rAnchor.mFit & AnchorBottom) ? -rCanvas.mVerticalDelta : 0.0f;
        if (x != 0.0f || y != 0.0f) {
            TranslateBranch(rOrder, place, x, y);
        }
    }

    void Layout::Draw(const DrawInfo& rInfo) {
        if (mPassDepth++ == 0) {
            FreeRetired();
        }
        RefreshOrder();
        DrawRange(0, mOrder.mNum, rInfo);
        mPassDepth--;
    }

    void Layout::DrawBranch(const Pane* pPane, const DrawInfo& rInfo) {
        if (mPassDepth++ == 0) {
            FreeRetired();
        }
        const u32 index = GetOrderIndex(pPane);
        if (index != Pane::cNoListIndex) {
            DrawRange(index, mOrder.mEnd[index], rInfo);
        }
        mPassDepth--;
    }

    void Layout::DrawRange(u32 first, u32 end, const DrawInfo& rInfo) {
        const Order order = mOrder;
        for (u32 i = first; i < end;) {
            Pane* const pPane = order.mPanes[i];
            if (!pPane->IsVisible()) {
                i = order.mEnd[i];
                continue;
            }
            MotionSignal::drawObject(pPane->mDrawSlot, mDrawGeneration);
            pPane->DrawSelf(rInfo);
            i++;
        }
    }

    void Layout::Animate(u32 opt) {
        if (mPassDepth++ == 0) {
            FreeRetired();
        }
        RefreshOrder();
        const Order order = mOrder;
        for (u32 i = 0; i < order.mNum;) {
            Pane* const pPane = order.mPanes[i];
            pPane->AnimateSelf(opt);
            i = pPane->IsVisible() || !(opt & 1) ? i + 1 : order.mEnd[i];
        }
        mPassDepth--;
    }

    const Rect Layout::GetLayoutRect() const {
        if (mOriginType == 1) {
            return Rect(-mLayoutSize.width / 2, mLayoutSize.height / 2, mLayoutSize.width / 2, -mLayoutSize.height / 2);
        }

        return Rect(0.0f, 0.0f, mLayoutSize.width, mLayoutSize.height);
    }

    void Layout::SetTagProcessor(TagProcessorBase< char16_t >* pTagProcessor) {
        SetTagProcessorImpl(mpRootPane, pTagProcessor);
    }
    Pane* Layout::BuildPaneObj(const UiPaneData* pRes, const ResBlockSet& resBlockSet) {
        switch (pRes->mKind) {
        case UiPaneKind_Pane:
            return NewPane< Pane >(pRes, resBlockSet);
        case UiPaneKind_Picture:
            return NewPane< Picture >(pRes, resBlockSet);
        case UiPaneKind_TextBox:
            return NewPane< TextBox >(pRes, resBlockSet);
        case UiPaneKind_Window:
            return NewPane< Window >(pRes, resBlockSet);
        case UiPaneKind_Bounding:
            return NewPane< Bounding >(pRes, resBlockSet);
        }
        return nullptr;
    }
}  // namespace Ui
