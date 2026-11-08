#include "Game/Screen/NebulaPaneKit.hpp"

#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>

namespace {
    // The holder's cooked layout, or none.
    nw4r::lyt::ResBlockSet blocksOf(LayoutHolder* pHolder) {
        if (pHolder == nullptr || pHolder->mLayoutRes.mCount == 0) {
            return nw4r::lyt::ResBlockSet{nullptr, nullptr, nullptr};
        }
        const UiLayoutHeader* const pLayout = static_cast< const UiLayoutHeader* >(pHolder->mLayoutRes.getRes(0u));
        if (pLayout == nullptr || pLayout->mMagic != cUiLayoutMagic || pLayout->mVersion != cUiDataVersion) {
            return nw4r::lyt::ResBlockSet{nullptr, nullptr, nullptr};
        }
        return nw4r::lyt::ResBlockSet{pLayout, pHolder, nullptr};
    }
}  // namespace

NebulaPaneKit::NebulaPaneKit()
    : mBlocks{nullptr, nullptr, nullptr}, mGame{nullptr, nullptr, nullptr}, mGameOpened(false), mpArchive(nullptr), mpInto(nullptr) {
}

bool NebulaPaneKit::init(const char* pArchive, nw4r::lyt::Layout* pInto) {
    return init(MR::createAndAddLayoutHolder(pArchive), pArchive, pInto);
}

bool NebulaPaneKit::init(LayoutHolder* pHolder, const char* pArchive, nw4r::lyt::Layout* pInto) {
    mBlocks = blocksOf(pHolder);
    mGame = nw4r::lyt::ResBlockSet{nullptr, nullptr, nullptr};
    mGameOpened = false;
    mpArchive = pArchive;
    mpInto = pInto;
    if (pInto == nullptr) {
        return false;
    }
    if (mBlocks.pLayout == nullptr) {
        mGameOpened = true;
        mBlocks = blocksOf(MR::createAndAddGameLayoutHolder(pArchive));
        if (mBlocks.pLayout == nullptr) {
            return false;
        }
        JUTNativeLog("%s: a mod's copy has no usable layout; the game's own is used\n", pArchive);
    }
    return true;
}

u32 NebulaPaneKit::find(const nw4r::lyt::ResBlockSet& rBlocks, const char* pName, s32 kind) {
    if (rBlocks.pLayout == nullptr) {
        return cUiNone16;
    }
    const u32 idx = uiFindPane(rBlocks.pLayout, pName);
    return idx != cUiNone16 && (kind < 0 || rBlocks.PaneAt(idx)->mKind == kind) ? idx : cUiNone16;
}

nw4r::lyt::Pane* NebulaPaneKit::make(const char* pName, const char* pNewName) const {
    return build(pName, pNewName, -1);
}

nw4r::lyt::Pane* NebulaPaneKit::build(const char* pName, const char* pNewName, s32 kind) const {
    const nw4r::lyt::ResBlockSet* pBlocks = &mBlocks;
    u32 idx = find(mBlocks, pName, kind);
    if (idx == cUiNone16) {
        if (!mGameOpened) {
            mGameOpened = true;
            mGame = blocksOf(MR::createAndAddGameLayoutHolder(mpArchive));
        }
        pBlocks = &mGame;
        idx = find(mGame, pName, kind);
        if (idx == cUiNone16) {
            return nullptr;
        }
        JUTNativeLog("%s: a mod's copy has no pane %s of its kind; the game's own is used\n", mpArchive, pName);
    }
    nw4r::lyt::Pane* pPane = mpInto->BuildPaneObj(pBlocks->PaneAt(idx), *pBlocks);
    if (pPane != nullptr) {
        pPane->SetName(pNewName);
        pPane->mTranslate = nw4r::math::VEC3(0.0f, 0.0f, 0.0f);
        pPane->SetVisible(true);
        pPane->SetInfluencedAlpha(true);
        // Placed by its screen, which keeps its own 16:9 width (the authored location adjust would narrow it again).
        nw4r::lyt::detail::SetBit(&pPane->mFlag, 2, false);
    }
    return pPane;
}

nw4r::lyt::Window* NebulaPaneKit::makeWindow(const char* pName, const char* pNewName) const {
    return nw4r::lyt::PaneCast< nw4r::lyt::Window >(build(pName, pNewName, UiPaneKind_Window));
}

nw4r::lyt::TextBox* NebulaPaneKit::makeText(const char* pName, const char* pNewName, u32 length) const {
    nw4r::lyt::TextBox* pText = nw4r::lyt::PaneCast< nw4r::lyt::TextBox >(build(pName, pNewName, UiPaneKind_TextBox));
    if (pText == nullptr) {
        return nullptr;
    }
    LayoutCoreUtil::initTextBoxPane(pText, nullptr, length);
    LayoutCoreUtil::setTextBoxMessage(pText, u"");
    return pText;
}
