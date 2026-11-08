#include "Game/Screen/LayoutGroupCtrl.hpp"
#include "Game/Animation/LayoutAnmPlayer.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include <Game/Ui/Group.hpp>

LayoutGroupCtrl::LayoutGroupCtrl(LayoutManager* pManager, const char* pName, u32 animLayerCount)
    : mManager(pManager), mGroup(), mAnmPlayerArray(animLayerCount) {
    mGroup = pManager->getGroup(pName);

    for (u32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i] = new LayoutAnmPlayer(pManager);
    }
}

void LayoutGroupCtrl::movement() {
    for (u32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->movement();
    }
}

void LayoutGroupCtrl::calcAnim() {
    for (u32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->reflectFrame();
    }
}

u32 LayoutGroupCtrl::getPaneNum() const {
    return mGroup->mPaneLinkList.Size();
}

Ui::Pane* LayoutGroupCtrl::getPane(u32 idx) const {
    for (Ui::detail::PaneLink* pLink : mGroup->GetPaneList()) {
        if (idx == 0) {
            return pLink->mTarget;
        }

        idx--;
    }

    return nullptr;
}

