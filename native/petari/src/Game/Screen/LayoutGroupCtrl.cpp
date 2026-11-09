#include "Game/Screen/LayoutGroupCtrl.hpp"
#include "Game/Animation/LayoutAnmPlayer.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include <Game/Ui/Group.hpp>

LayoutGroupCtrl::LayoutGroupCtrl(LayoutManager* pManager, const char* pName, u32 animLayerCount)
    : mManager(pManager), mGroup(), mAnmPlayerArray(animLayerCount) {
    mGroup = pManager->getGroup(pName);

    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i] = new LayoutAnmPlayer(pManager);
    }
}

void LayoutGroupCtrl::movement() {
    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->movement();
    }
}

void LayoutGroupCtrl::calcAnim() {
    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->reflectFrame();
    }
}

u32 LayoutGroupCtrl::getPaneNum() const {
    // A group the layout lacks (a mod's layout) holds no panes.
    if (mGroup == nullptr) {
        return 0;
    }

    return mGroup->mPaneLinkList.Size();
}

Ui::Pane* LayoutGroupCtrl::getPane(u32 idx) const {
    if (mGroup == nullptr) {
        return nullptr;
    }

    for (Ui::detail::PaneLink* pLink : mGroup->GetPaneList()) {
        if (idx == 0) {
            return pLink->mTarget;
        }

        idx--;
    }

    return nullptr;
}

