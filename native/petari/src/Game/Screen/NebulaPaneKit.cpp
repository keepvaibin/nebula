#include "Game/Screen/NebulaPaneKit.hpp"

#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <nebula_module.h>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Window.hpp>

namespace {
    // The holder's cooked layout, or none.
    Ui::ResBlockSet blocksOf(LayoutHolder* pHolder) {
        if (pHolder == nullptr || pHolder->mLayoutRes.mCount == 0) {
            return Ui::ResBlockSet{nullptr, nullptr, nullptr};
        }
        const UiLayoutHeader* const pLayout = static_cast< const UiLayoutHeader* >(pHolder->mLayoutRes.getRes(0u));
        if (pLayout == nullptr || pLayout->mMagic != cUiLayoutMagic || pLayout->mVersion != cUiDataVersion) {
            if (pLayout != nullptr && pLayout->mMagic == cUiLayoutMagic) {
                nebula_log("UiData: a layout of version %u; this game reads version %u (the pak needs a new cook)\n",
                        static_cast< unsigned >(pLayout->mVersion), static_cast< unsigned >(cUiDataVersion));
            }
            return Ui::ResBlockSet{nullptr, nullptr, nullptr};
        }
        return Ui::ResBlockSet{pLayout, pHolder, nullptr};
    }
}  // namespace

NebulaPaneKit::NebulaPaneKit()
    : mBlocks{nullptr, nullptr, nullptr}, mGame{nullptr, nullptr, nullptr}, mGameOpened(false), mpArchive(nullptr), mpInto(nullptr) {
}

bool NebulaPaneKit::init(const char* pArchive, Ui::Layout* pInto) {
    return init(MR::createAndAddLayoutHolder(pArchive), pArchive, pInto);
}

bool NebulaPaneKit::init(LayoutHolder* pHolder, const char* pArchive, Ui::Layout* pInto) {
    mBlocks = blocksOf(pHolder);
    mGame = Ui::ResBlockSet{nullptr, nullptr, nullptr};
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
        nebula_log("%s: a mod's copy has no usable layout; the game's own is used\n", pArchive);
    }
    return true;
}

u32 NebulaPaneKit::find(const Ui::ResBlockSet& rBlocks, const char* pName, s32 kind) {
    if (rBlocks.pLayout == nullptr) {
        return cUiNone16;
    }
    const u32 idx = uiFindPane(rBlocks.pLayout, pName);
    return idx != cUiNone16 && (kind < 0 || rBlocks.PaneAt(idx)->mKind == kind) ? idx : cUiNone16;
}

Ui::Pane* NebulaPaneKit::make(const char* pName, const char* pNewName) const {
    return build(pName, pNewName, -1);
}

Ui::Pane* NebulaPaneKit::build(const char* pName, const char* pNewName, s32 kind) const {
    const Ui::ResBlockSet* pBlocks = &mBlocks;
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
        nebula_log("%s: a mod's copy has no pane %s of its kind; the game's own is used\n", mpArchive, pName);
    }
    Ui::Pane* pPane = mpInto->BuildPaneObj(pBlocks->PaneAt(idx), *pBlocks);
    if (pPane != nullptr) {
        pPane->SetName(pNewName);
        pPane->mTranslate = Vec3f(0.0f, 0.0f, 0.0f);
        pPane->SetVisible(true);
        pPane->SetInfluencedAlpha(true);
        // Placed by its screen, which keeps its own 16:9 width (the authored location adjust would narrow it again).
        Ui::detail::SetBit(&pPane->mFlag, 2, false);
    }
    return pPane;
}

Ui::Window* NebulaPaneKit::makeWindow(const char* pName, const char* pNewName) const {
    return Ui::PaneCast< Ui::Window >(build(pName, pNewName, UiPaneKind_Window));
}

Ui::TextBox* NebulaPaneKit::makeText(const char* pName, const char* pNewName, u32 length) const {
    Ui::TextBox* pText = Ui::PaneCast< Ui::TextBox >(build(pName, pNewName, UiPaneKind_TextBox));
    if (pText == nullptr) {
        return nullptr;
    }
    LayoutCoreUtil::initTextBoxPane(pText, nullptr, length);
    LayoutCoreUtil::setTextBoxMessage(pText, u"");
    return pText;
}
