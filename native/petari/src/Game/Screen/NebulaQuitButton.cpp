#include "Game/Screen/NebulaQuitButton.hpp"

#ifdef TARGET_PC
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstring>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>

namespace {
    // The button waits this long after its corner frees up: a pointer crossing between files does not flash it.
    const s32 cShowDelayFrames = 8;

    // The label in the played language (NebulaText; the game's own Yes and No answer the question).
    const wchar_t* getLabel() {
        static wchar_t label[64];
        NebulaText::get(label, 64, "QuitToDesktop");
        return label;
    }

    nw4r::lyt::TextBox* asTextBox(nw4r::lyt::Pane* pPane) {
        if (pPane == nullptr || !pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::TextBox::typeInfo)) {
            return nullptr;
        }
        return static_cast< nw4r::lyt::TextBox* >(pPane);
    }

    // The pill's straight middle (one of its three pieces) and its two caps, which hold on to the middle's ends.
    void setPillMiddle(const LayoutActor* pActor, const char* pMiddle, const char* pRight, const char* pLeft, f32 width) {
        nw4r::lyt::Pane* pPane = MR::getPane(pActor, pMiddle);
        if (pPane != nullptr) {
            pPane->mSize.width = width;
        }
        if ((pPane = MR::getPane(pActor, pRight)) != nullptr) {
            pPane->mTranslate.x = width * 0.5f;
        }
        if ((pPane = MR::getPane(pActor, pLeft)) != nullptr) {
            pPane->mTranslate.x = -width * 0.5f;
        }
    }
};  // namespace

NebulaQuitButton::NebulaQuitButton(const char* pName) : LayoutActor(pName, true), mButton(nullptr), mShownFrames(0), mIsDecided(false) {
}

void NebulaQuitButton::init(const JMapInfoIter& rIter) {
    initLayoutManager("FileSelect", 1);

    // Its own name for the canvas rules (LayoutAspect): the file select's Start follows the right and bottom edges,
    // this copy of it the left and top.
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaQuitButton";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    MR::hidePane(this, "CopyButton");
    MR::hidePane(this, "MiiButton");
    MR::hidePane(this, "DeleteButton");
    MR::hidePane(this, "P2ManualButton");
    MR::createAndAddPaneCtrl(this, "StartButton", 1);
    layOut();

    mButton = new ButtonPaneController(this, "StartButton", "BoxStartButton", 0, true);
    mButton->_22 = false;
    MR::connectToSceneLayout(this);
    LayoutActor::appear();
}

// Every size and place from the layout's own values: the label at the size of the file select's secondary buttons
// (Copy, Mii, Delete) against its Start; the pill a capsule around it (its straight middle as wide as the label, the
// rounded caps its padding); its top and left edges as far from the canvas's top-left corner as Start's bottom edge is
// from the bottom, and the pill grows to the right when pointed, so its left edge never nears the screen's.
void NebulaQuitButton::layOut() {
    nw4r::lyt::Pane* pRoot = getLayoutManager()->mLayout->mpRootPane;
    nw4r::lyt::Pane* pStart = MR::getPane(this, "StartButton");
    nw4r::lyt::Pane* pButton = MR::getPane(this, "SButton");
    nw4r::lyt::Pane* pPicture = MR::getPane(this, "PicSButton");
    nw4r::lyt::Pane* pMiddle = MR::getPane(this, "PicCButton");
    nw4r::lyt::Pane* pCap = MR::getPane(this, "PicRButton");
    nw4r::lyt::Pane* pTextParent = MR::getPane(this, "StartButtonTxt");
    nw4r::lyt::TextBox* pSecondary = asTextBox(MR::getPane(this, "TxtCopy"));
    nw4r::lyt::Pane* pBound = MR::getPane(this, "BoxStartButton");
    if (pStart == nullptr || pButton == nullptr || pPicture == nullptr || pMiddle == nullptr || pCap == nullptr || pTextParent == nullptr ||
        pSecondary == nullptr) {
        return;
    }

    MR::setTextBoxMessageRecursive(this, "StartButtonTxt", getLabel());
    nw4r::lyt::TextBox* pLabel = nullptr;
    for (nw4r::lyt::PaneList::Iterator it = pTextParent->GetChildList().GetBeginIter(); it != pTextParent->GetChildList().GetEndIter(); ++it) {
        nw4r::lyt::TextBox* pText = asTextBox(&*it);
        if (pText != nullptr && strncmp(pText->mName, "Txt", 3) == 0) {
            pLabel = pText;
        }
    }
    if (pLabel == nullptr || !(pLabel->mFontSize.height > 0.0f)) {
        return;
    }

    // The authored Start: where its pane sits, how far its pill's outer edges lie from the pane's origin.
    const f32 pictureScale = pPicture->mScale.x;
    const f32 middle0 = pMiddle->mSize.width;
    const f32 cap = pCap->mSize.width * pCap->mScale.x;
    const f32 height = pMiddle->mSize.height * pPicture->mScale.y;
    const f32 startX = pStart->mTranslate.x;
    const f32 startY = pStart->mTranslate.y;
    const f32 inset = pRoot->mSize.height * 0.5f + startY - height * 0.5f;

    // This pill: a capsule, its straight middle as wide as the label.
    const f32 scale = pSecondary->mFontSize.height / pLabel->mFontSize.height;
    const nw4r::ut::Rect rect = pLabel->GetTextDrawRect();
    const f32 labelWidth = rect.right - rect.left;
    const f32 middle = labelWidth / pictureScale;
    setPillMiddle(this, "PicCFrame", "PicRFrame", "PicLFrame", middle);
    setPillMiddle(this, "PicCButton", "PicRButton", "PicLButton", middle);
    setPillMiddle(this, "PicCShine", "PicRShine", "PicLShine", middle);
    setPillMiddle(this, "ShaCButton", "ShaRButton", "ShaLButton", middle);
    if (pBound != nullptr) {
        pBound->mSize.width += middle - middle0;
    }
    for (nw4r::lyt::PaneList::Iterator it = pTextParent->GetChildList().GetBeginIter(); it != pTextParent->GetChildList().GetEndIter(); ++it) {
        if (it->mSize.width < labelWidth) {
            it->mSize.width = labelWidth;
        }
    }

    // The button pane (SButton, which the animations scale) holds the pill by its left edge: the pill's pieces move
    // right by half its width.
    const f32 halfWidth = pictureScale * (middle * 0.5f + cap);
    for (nw4r::lyt::PaneList::Iterator it = pButton->GetChildList().GetBeginIter(); it != pButton->GetChildList().GetEndIter(); ++it) {
        it->mTranslate.x += halfWidth;
    }

    // Start's pane mirrored through the canvas centre (it follows the left and top edges as Start the right and
    // bottom), the pill's top and left edges `inset` in from the canvas's.
    pStart->mScale.x = scale;
    pStart->mScale.y = scale;
    pStart->mTranslate.x = -startX;
    pStart->mTranslate.y = pRoot->mSize.height * 0.5f - inset - height * 0.5f * scale;
    pButton->mTranslate.x = (-pRoot->mSize.width * 0.5f + inset + startX) / scale;
}

void NebulaQuitButton::control() {
    if (mButton->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }

    if (!mIsDecided && mButton->trySelect()) {
        mIsDecided = true;
    }

    mButton->update();
}

bool NebulaQuitButton::takeMenuFocus() {
    if (!mButton->isWait() && !mButton->isNotPointing()) {
        return false;
    }
    NebulaMenuFocus::setFocus(mButton);
    return true;
}

void NebulaQuitButton::setShown(bool shown) {
    if (!shown) {
        mShownFrames = 0;
        if (!mButton->isHidden() && !mButton->isDisappearing() && !mButton->isAppearing() && !mIsDecided) {
            mButton->disappear();
        }
        return;
    }

    if (mShownFrames < cShowDelayFrames) {
        mShownFrames++;
    }
    if (mShownFrames >= cShowDelayFrames && mButton->isHidden()) {
        mIsDecided = false;
        mButton->appear();
    }
}

bool NebulaQuitButton::isDecided() const {
    return mIsDecided;
}

void NebulaQuitButton::resume() {
    mIsDecided = false;
    mButton->forceToWait();
}

bool NebulaQuitButton::isPointing() const {
    return mButton->isPointing();
}

// The question the button asks, in the played language.
const wchar_t* NebulaQuitButton::getQuestion() {
    static wchar_t question[128];
    NebulaText::get(question, 128, "QuitQuestion");
    return question;
}
#endif
