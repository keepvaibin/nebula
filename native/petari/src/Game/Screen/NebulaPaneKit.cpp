#include "Game/Screen/NebulaPaneKit.hpp"

#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/LayoutHolder.hpp"
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>

NebulaPaneKit::NebulaPaneKit() : mBlocks{nullptr, nullptr, nullptr} {
}

bool NebulaPaneKit::init(LayoutHolder* pHolder) {
    mBlocks = nw4r::lyt::ResBlockSet{nullptr, nullptr, nullptr};
    if (pHolder == nullptr || pHolder->mLayoutRes.mCount == 0) {
        return false;
    }
    const UiLayoutHeader* const pLayout = static_cast< const UiLayoutHeader* >(pHolder->mLayoutRes.getRes(0u));
    if (pLayout == nullptr || pLayout->mMagic != cUiLayoutMagic || pLayout->mVersion != cUiDataVersion) {
        return false;
    }
    mBlocks = nw4r::lyt::ResBlockSet{pLayout, pHolder, nullptr};
    return true;
}

nw4r::lyt::Pane* NebulaPaneKit::make(const char* pName, const char* pNewName) const {
    if (mBlocks.pLayout == nullptr) {
        return nullptr;
    }
    const u32 idx = uiFindPane(mBlocks.pLayout, pName);
    if (idx == cUiNone16) {
        return nullptr;
    }
    nw4r::lyt::Pane* pPane = nw4r::lyt::Layout::BuildPaneObj(mBlocks.PaneAt(idx), mBlocks);
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
    nw4r::lyt::Pane* pPane = make(pName, pNewName);
    nw4r::lyt::Window* pWindow = pPane != nullptr ? nw4r::ut::DynamicCast< nw4r::lyt::Window* >(pPane) : nullptr;
    if (pPane != nullptr && pWindow == nullptr) {
        nw4r::lyt::Layout::DeleteObj(pPane);
    }
    return pWindow;
}

nw4r::lyt::TextBox* NebulaPaneKit::makeText(const char* pName, const char* pNewName, u32 length) const {
    nw4r::lyt::Pane* pPane = make(pName, pNewName);
    if (pPane == nullptr) {
        return nullptr;
    }
    if (!pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::TextBox::typeInfo)) {
        nw4r::lyt::Layout::DeleteObj(pPane);
        return nullptr;
    }
    nw4r::lyt::TextBox* pText = static_cast< nw4r::lyt::TextBox* >(pPane);
    LayoutCoreUtil::initTextBoxPane(pText, nullptr, length);
    LayoutCoreUtil::setTextBoxMessage(pText, u"");
    return pText;
}
