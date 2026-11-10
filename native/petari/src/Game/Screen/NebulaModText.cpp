#include "Game/Screen/NebulaModText.hpp"
#include <nebula_module.h>

#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/TextBox.hpp>
#include <cstring>
#include "Game/System/LumaCore.hpp"

NebulaModText::NebulaModText()
    : LayoutActor("NebulaModText", true), mKit(nullptr), mGroup(nullptr), mLines(), mRevision(0), mScaleX(1.0f), mWidth(0.0f),
      mHeight(0.0f), mAny(false) {
    lumacore::setClass(this, lumacore::kClassId< NebulaModText >);
}

void NebulaModText::init(const TableIter& /*rIter*/) {
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaModText";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    Ui::Pane* pRoot = pManager->mLayout->mpRootPane;
    mKit = new NebulaPaneKit();
    if (pRoot == nullptr || !mKit->init(pManager->mLayoutHolder, "InformationWindow", pManager->mLayout)) {
        nebula_log("NebulaModText: no InformationWindow layout\n");
        mGroup = nullptr;
        return;
    }
    // Only the lines: the information window's own panes stay hidden.
    for (Ui::Pane* pChild : pRoot->GetChildList()) {
        pChild->SetVisible(false);
    }
    mGroup = NebulaUi::makeGroup(pRoot, "NbModText");
    NebulaUi::Kits kits = {mKit, nullptr, nullptr};
    for (s32 i = 0; i < NebulaMods::cTextLines; i++) {
        char name[16] = "NbModLine";
        name[9] = static_cast< char >('0' + i / 10);
        name[10] = static_cast< char >('0' + i % 10);
        name[11] = 0;
        mLines[i] = NebulaUi::makeText(kits, mGroup, name, NebulaMods::cTextLength);
        if (mLines[i] != nullptr) {
            mLines[i]->SetVisible(false);
        }
    }
    mRevision = NebulaMods::textRevision() - 1u;
    LayoutActor::appear();
}

// The lines in screen units (the layout's units made square): x 0-1 across from the left edge, y 0-1 down from the top.
void NebulaModText::place() {
    mGroup->mScale = Vec2f(mScaleX, 1.0f);
    mAny = false;
    for (s32 i = 0; i < NebulaMods::cTextLines; i++) {
        Ui::TextBox* pText = mLines[i];
        const NebulaMods::TextLine& rLine = NebulaMods::textLine(i);
        if (pText == nullptr) {
            continue;
        }
        pText->SetVisible(rLine.mShown);
        if (!rLine.mShown) {
            continue;
        }
        mAny = true;
        NebulaUi::setTextStyle(pText, rLine.mSize * mHeight, rLine.mRgba, rLine.mRgba, 0);
        NebulaUi::setText(pText, rLine.mText);
        pText->mTranslate = Vec3f((rLine.mX - 0.5f) * mWidth, (0.5f - rLine.mY) * mHeight, 0.0f);
    }
}

void NebulaModText::update() {
    if (mGroup == nullptr) {
        return;
    }
    f32 scaleX, width, height;
    NebulaUi::readScreen(&scaleX, &width, &height);
    const u32 revision = NebulaMods::textRevision();
    if (revision != mRevision || scaleX != mScaleX || width != mWidth || height != mHeight) {
        mRevision = revision;
        mScaleX = scaleX;
        mWidth = width;
        mHeight = height;
        place();
    }
    if (mAny) {
        movement();
        calcAnim();
    }
}

void NebulaModText::drawOnTop() const {
    if (mGroup != nullptr && mAny) {
        draw();
    }
}
