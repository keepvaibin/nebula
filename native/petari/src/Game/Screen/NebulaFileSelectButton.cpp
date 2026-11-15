#include "Game/Screen/NebulaFileSelectButton.hpp"

#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/TextBox.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    // A button waits this long after its corner frees up: a pointer crossing between files does not flash it.
    const s32 cShowDelayFrames = 8;

    // Each button's own layout name, for the canvas rules (LayoutAspect): Quit follows the left and top edges, Settings
    // the right and top.
    const char cQuitLayoutName[] = "NebulaQuitButton";
    const char cSettingsLayoutName[] = "NebulaSettingsButton";

    // The labels in the played language (NebulaText; the game's own Yes and No answer Quit's question).
    const char16_t* getQuitLabel() {
        static MR::GrowBuffer< char16_t > label;
        NebulaUi::getText(label, "Quit");
        return label.mItems;
    }

    const char16_t* getSettingsLabel() {
        static MR::GrowBuffer< char16_t > label;
        NebulaUi::getText(label, "SettingsTitle");
        return label.mItems;
    }

    Ui::TextBox* asTextBox(Ui::Pane* pPane) {
        return Ui::PaneCast< Ui::TextBox >(pPane);
    }

    // The pill's straight middle (one of its three pieces) and its two caps, which hold on to the middle's ends.
    void setPillMiddle(const LayoutActor* pActor, const char* pMiddle, const char* pRight, const char* pLeft, f32 width) {
        Ui::Pane* pPane = MR::getPane(pActor, pMiddle);
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

NebulaFileSelectButton::NebulaFileSelectButton(const char* pName, Role role)
    : LayoutActor(pName, true), mButton(nullptr), mRole(role), mShownFrames(0), mIsDecided(false) {
    lumacore::setClass(this, lumacore::kClassId< NebulaFileSelectButton >);
}

void NebulaFileSelectButton::init(const TableIter& /*rIter*/) {
    initLayoutManager("FileSelect", 1);

    LayoutManager* pManager = getLayoutManager();
    const char* const pLayoutName = mRole == Role_Settings ? cSettingsLayoutName : cQuitLayoutName;
    const u32 size = static_cast< u32 >(strlen(pLayoutName)) + 1;
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[size];
    memcpy(pManager->mLayoutName, pLayoutName, size);

    MR::hidePane(this, "CopyButton");
    MR::hidePane(this, "MiiButton");
    MR::hidePane(this, "DeleteButton");
    MR::hidePane(this, "P2ManualButton");
    MR::createAndAddPaneCtrl(this, "StartButton", 1);
    layOut();

    mButton = new ButtonPaneController(this, "StartButton", "BoxStartButton", 0, true);
    mButton->mDisappearsAfterDecide = false;
    MR::connectToSceneLayout(this);
    LayoutActor::appear();
}

// Every size and place from the layout's own values: the label at the size of the file select's secondary buttons
// (Copy, Mii, Delete) against its Start; the pill a capsule around it (its straight middle as wide as the label, the
// rounded caps its padding); its top and outer edges as far from the canvas's corner as Start's bottom edge is from
// the bottom. The pill grows toward the screen's middle when pointed, so its outer edge never nears the screen's.
void NebulaFileSelectButton::layOut() {
    Ui::Pane* pRoot = getLayoutManager()->mLayout->mpRootPane;
    Ui::Pane* pStart = MR::getPane(this, "StartButton");
    Ui::Pane* pButton = MR::getPane(this, "SButton");
    Ui::Pane* pPicture = MR::getPane(this, "PicSButton");
    Ui::Pane* pMiddle = MR::getPane(this, "PicCButton");
    Ui::Pane* pCap = MR::getPane(this, "PicRButton");
    Ui::Pane* pTextParent = MR::getPane(this, "StartButtonTxt");
    Ui::TextBox* pSecondary = asTextBox(MR::getPane(this, "TxtCopy"));
    Ui::Pane* pBound = MR::getPane(this, "BoxStartButton");
    if (pStart == nullptr || pButton == nullptr || pPicture == nullptr || pMiddle == nullptr || pCap == nullptr || pTextParent == nullptr ||
        pSecondary == nullptr) {
        return;
    }

    MR::setTextBoxMessageRecursive(this, "StartButtonTxt", mRole == Role_Settings ? getSettingsLabel() : getQuitLabel());
    Ui::TextBox* pLabel = nullptr;
    for (Ui::Pane* pChild : pTextParent->GetChildList()) {
        Ui::TextBox* pText = asTextBox(pChild);
        if (pText != nullptr && MR::isEqualStringPrefix(pText->mName, "Txt")) {
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
    const Ui::Rect rect = pLabel->GetTextDrawRect();
    const f32 labelWidth = rect.right - rect.left;
    const f32 middle = labelWidth / pictureScale;
    setPillMiddle(this, "PicCFrame", "PicRFrame", "PicLFrame", middle);
    setPillMiddle(this, "PicCButton", "PicRButton", "PicLButton", middle);
    setPillMiddle(this, "PicCShine", "PicRShine", "PicLShine", middle);
    setPillMiddle(this, "ShaCButton", "ShaRButton", "ShaLButton", middle);
    if (pBound != nullptr) {
        pBound->mSize.width += middle - middle0;
    }
    for (Ui::Pane* pChild : pTextParent->GetChildList()) {
        if (pChild->mSize.width < labelWidth) {
            pChild->mSize.width = labelWidth;
        }
    }

    // The button pane (SButton, which the animations scale) holds the pill by its outer edge: Quit's pieces move right
    // by half the pill's width, Settings' left.
    const f32 halfWidth = pictureScale * (middle * 0.5f + cap);
    const f32 toward = mRole == Role_Settings ? -halfWidth : halfWidth;
    for (Ui::Pane* pChild : pButton->GetChildList()) {
        pChild->mTranslate.x += toward;
    }

    // Quit: Start's pane mirrored through the canvas centre (it follows the left and top edges as Start the right and
    // bottom). Settings: Start's own side, mirrored top for bottom. Either pill's top and outer edges `inset` in from
    // the canvas's.
    pStart->mScale.x = scale;
    pStart->mScale.y = scale;
    pStart->mTranslate.y = pRoot->mSize.height * 0.5f - inset - height * 0.5f * scale;
    if (mRole == Role_Settings) {
        pStart->mTranslate.x = startX;
        pButton->mTranslate.x = (pRoot->mSize.width * 0.5f - inset - startX) / scale;
    } else {
        pStart->mTranslate.x = -startX;
        pButton->mTranslate.x = (-pRoot->mSize.width * 0.5f + inset + startX) / scale;
    }
}

void NebulaFileSelectButton::control() {
    if (mButton->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }

    if (!mIsDecided && mButton->trySelect()) {
        mIsDecided = true;
    }

    mButton->update();
}

bool NebulaFileSelectButton::takeMenuFocus() {
    if (!mButton->isWait() && !mButton->isNotPointing()) {
        return false;
    }
    NebulaMenuFocus::setFocus(mButton);
    return true;
}

void NebulaFileSelectButton::setShown(bool shown) {
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

bool NebulaFileSelectButton::isDecided() const {
    return mIsDecided;
}

void NebulaFileSelectButton::resume() {
    mIsDecided = false;
    mButton->forceToWait();
}

bool NebulaFileSelectButton::isPointing() const {
    return mButton->isPointing();
}

// The question Quit asks, in the played language.
const char16_t* NebulaFileSelectButton::getQuitQuestion() {
    static MR::GrowBuffer< char16_t > question;
    NebulaUi::getText(question, "QuitQuestion");
    return question.mItems;
}
