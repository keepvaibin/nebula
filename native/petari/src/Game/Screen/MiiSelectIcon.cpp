#include "Game/Screen/MiiSelectIcon.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/NPC/MiiFaceIcon.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <Game/Ui/TexMap.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sCharacterTexFrame[] = {
        0.0f, 4.0f, 3.0f, 2.0f, 1.0f,
    };

    NEW_NERVE(MiiSelectIconNrvCreate, MiiSelectIcon, Create);
    NEW_NERVE(MiiSelectIconNrvWait, MiiSelectIcon, Wait);
    NEW_NERVE(MiiSelectIconNrvSelected, MiiSelectIcon, Selected);
    NEW_NERVE(MiiSelectIconNrvInvalid, MiiSelectIcon, Invalid);
};  // namespace

MiiSelectIcon::MiiSelectIcon(int movementType, int calcAnimType, int drawType, const char* pName)
    : LayoutActor(pName, true), mActiveButton(), mFellowButton(), mMiiButton(), mIcon(), mMiiTexMap(), mFellowTexMap(), mIconID(new FileSelectIconID()), mIsMiiDummy(),
      mIsEnabled(true) {
    lumacore::setClass(this, lumacore::kClassId< MiiSelectIcon >);
    initLayoutManager("MiiIcon", 1);
    MR::createAndAddPaneCtrl(this, "MarioIcon", 3);
    MR::createAndAddPaneCtrl(this, "MiiIcon", 3);
    MR::invalidateParentAnim(this);
    createButton();
    createFaceImageObj();
    initNerve(GET_NERVE_ANON(MiiSelectIconNrvCreate));

    if (movementType < 0) {
        movementType = MR::MovementType_Layout;
    }

    if (calcAnimType < 0) {
        calcAnimType = MR::CalcAnimType_Layout;
    }

    if (drawType < 0) {
        drawType = MR::DrawType_LayoutDecoration;
    }

    MR::connectToScene(this, movementType, calcAnimType, MR::DrawBufferType_None, drawType);
}

void MiiSelectIcon::appear(const FileSelectIconID& rIconID) {
    if (MR::isDead(this)) {
        appear();
    }

    mIconID->set(rIconID);

    if (rIconID.isFellow()) {
        MR::showLayout(this);
        MR::startAnim(this, "Character", 0);
        MR::setAnimFrameAndStop(this, ::sCharacterTexFrame[rIconID.getFellowID()], 0);
        MR::startPaneAnim(this, "MarioIcon", "Character", 1);
        MR::setPaneAnimFrameAndStop(this, "MarioIcon", ::sCharacterTexFrame[rIconID.getFellowID()], 1);

        mActiveButton = mFellowButton;

        MR::hidePane(this, "MiiIcon");
        MR::showPane(this, "MarioIcon");
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvWait));
    } else if (rIconID.isMii()) {
        MR::hideLayout(this);
        mIcon->setIndex(rIconID.getMiiIndex());

        Ui::TexMap* pTexMap = mMiiTexMap;

        MR::replacePaneTexture(this, "ShaMii", pTexMap, 0);
        MR::replacePaneTexture(this, "PicMii", pTexMap, 0);
        MR::replacePaneTexture(this, "PicMiiShine", pTexMap, 0);

        mActiveButton = mMiiButton;

        MR::hidePane(this, "MarioIcon");
        MR::showPane(this, "MiiIcon");
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvCreate));
    }

    mIsMiiDummy = false;

    MR::startPaneAnim(this, "MarioIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MarioIcon", 0.0f, 2);
    MR::startPaneAnim(this, "MiiIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MiiIcon", 0.0f, 2);
}

void MiiSelectIcon::appearMiiDummy() {
    if (MR::isDead(this)) {
        appear();
    }

    MR::showLayout(this);
    MR::startAnim(this, "Character", 0);
    MR::setAnimFrameAndStop(this, 5.0f, 0);
    MR::startPaneAnim(this, "MarioIcon", "Character", 1);
    MR::setPaneAnimFrameAndStop(this, "MarioIcon", 5.0f, 1);
    mActiveButton = mFellowButton;
    MR::hidePane(this, "MiiIcon");
    MR::showPane(this, "MarioIcon");
    setNerve(GET_NERVE_ANON(MiiSelectIconNrvWait));
    mIsMiiDummy = true;
    MR::startPaneAnim(this, "MarioIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MarioIcon", 0.0f, 2);
    MR::startPaneAnim(this, "MiiIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MiiIcon", 0.0f, 2);
}

void MiiSelectIcon::validate() {
    if (isNerve(GET_NERVE_ANON(MiiSelectIconNrvSelected)) || isNerve(GET_NERVE_ANON(MiiSelectIconNrvInvalid))) {
        mFellowButton->forceToWait();
        mMiiButton->forceToWait();
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvWait));
    }

    mIsEnabled = true;
}

void MiiSelectIcon::invalidate() {
    if (isNerve(GET_NERVE_ANON(MiiSelectIconNrvSelected))) {
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvInvalid));
    }

    mIsEnabled = false;
}

void MiiSelectIcon::prohibit() {
    invalidate();
    MR::startPaneAnim(this, "MarioIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MarioIcon", 1.0f, 2);
    MR::startPaneAnim(this, "MiiIcon", "Hide", 2);
    MR::setPaneAnimFrameAndStop(this, "MiiIcon", 1.0f, 2);
}

bool MiiSelectIcon::isSelected() const {
    return isNerve(GET_NERVE_ANON(MiiSelectIconNrvSelected));
}

bool MiiSelectIcon::isMiiDummy() const {
    return mIsMiiDummy;
}

void MiiSelectIcon::copyIconID(FileSelectIconID* pIconID) {
    pIconID->set(*mIconID);
}

Ui::TexMap* MiiSelectIcon::getTexMap() {
    if (mIconID->isFellow()) {
        // A layout without the PicMario picture keeps the empty fellow texture.
        const Ui::TexMap* pPicMario = MR::getLytTexMap(this, "PicMario", 0);
        if (pPicMario != nullptr) {
            SiteTexture texObj;

            pPicMario->Get(&texObj);
            mFellowTexMap->Set(texObj);
        }

        return mFellowTexMap;
    }

    return mMiiTexMap;
}

bool MiiSelectIcon::isPointing() const {
    return mActiveButton->isPointing();
}

void MiiSelectIcon::exeCreate() {
    if (MR::isFirstStep(this)) {
        mIcon->requestToMakeIcon();
    }

    if (mIcon->mIsCreated) {
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvWait));
    }
}

void MiiSelectIcon::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
        mActiveButton->appear();
    }

    if (mIsEnabled) {
        mActiveButton->trySelect();
    }

    if (mActiveButton->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }

    if (mActiveButton->mIsSelected) {
        setNerve(GET_NERVE_ANON(MiiSelectIconNrvSelected));
    }
}

void MiiSelectIcon::exeSelected() {
}

void MiiSelectIcon::exeDisappear() {
    if (mActiveButton->isHidden()) {
        if (mActiveButton == mFellowButton) {
            mMiiButton->forceToHide();
        } else {
            mFellowButton->forceToHide();
        }

        kill();
    }
}

void MiiSelectIcon::exeInvalid() {
}

void MiiSelectIcon::appear() {
    LayoutActor::appear();
}

void MiiSelectIcon::control() {
    mFellowButton->update();
    mMiiButton->update();
}

void MiiSelectIcon::createButton() {
    mFellowButton = new ButtonPaneController(this, "MarioIcon", "PicMario", 0, true);
    mFellowButton->mDisappearsAfterDecide = false;

    mMiiButton = new ButtonPaneController(this, "MiiIcon", "PicMii", 0, true);
    mMiiButton->mDisappearsAfterDecide = false;
    // MiiSelect turns its pages with the D-pad and stick (NebulaMenuFocus leaves its icons to the pointer).
    mFellowButton->mIsNebulaFocusable = false;
    mMiiButton->mIsNebulaFocusable = false;
}

void MiiSelectIcon::createFaceImageObj() {
    Ui::TexMap* pTexMap;

    mIcon =
        new MiiFaceIcon(128, 128, MiiFaceRecipe(Mii::SourceLibrary, 0, 256, Mii::expressionBit(Mii::ExpressionNormal) | Mii::expressionBit(Mii::ExpressionBlink)), "Miiアイコン");
    mIcon->initWithoutIter();

    // A layout without the PicMario picture gets an empty fellow texture.
    const Ui::TexMap* pPicMario = MR::getLytTexMap(this, "PicMario", 0);
    if (pPicMario != nullptr) {
        SiteTexture texObj;
        pPicMario->Get(&texObj);
        mFellowTexMap = new Ui::TexMap(texObj);
    } else {
        mFellowTexMap = new Ui::TexMap();
    }
    mMiiTexMap = MR::createLytTexMap(reinterpret_cast< ResTIMG* >(mIcon->mImageBuffer));
    pTexMap = mMiiTexMap;

    MR::replacePaneTexture(this, "ShaMii", pTexMap, 0);
    MR::replacePaneTexture(this, "PicMii", pTexMap, 0);
    MR::replacePaneTexture(this, "PicMiiShine", pTexMap, 0);
}
