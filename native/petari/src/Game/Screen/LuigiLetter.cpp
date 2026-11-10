#include "Game/Screen/LuigiLetter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstdio>
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvLuigiLetter {
    NEW_NERVE(HostTypeNrvAppear, LuigiLetter, Appear);
    NEW_NERVE(HostTypeNrvWait, LuigiLetter, Wait);
    NEW_NERVE(HostTypeNrvEnd, LuigiLetter, End);
};  // namespace NrvLuigiLetter

LuigiLetter::LuigiLetter(bool param1, const char* pParam2)
    : LayoutActor("ルイージの手紙", true), mTexture(nullptr), mAButtonIcon(nullptr), mGalaxyName(pParam2), mIsForTalk(param1) {
    lumacore::setClass(this, lumacore::kClassId< LuigiLetter >);
}

void LuigiLetter::init(const TableIter& /*rIter*/) {
    MR::FormattedName archiveName;
    MR::FormattedName textureName;

    initLayoutManager("LuigiLetter", 1);
    initNerve(GET_NERVE(LuigiLetter, HostTypeNrvAppear));
    archiveName.format("LuigiPicture%s", mGalaxyName);
    textureName.format("LuigiPicture%s.bti", mGalaxyName);

    mTexture = MR::createLytTexMap(archiveName.c_str(), textureName.c_str());

    MR::replacePaneTexture(this, "PicDummy", mTexture, 0);

    mAButtonIcon = new IconAButton(false, false);
    mAButtonIcon->initWithoutIter();
    mAButtonIcon->setFollowActorPane(this, "AButtonPosition");

    if (mIsForTalk) {
        MR::connectToSceneTalkLayoutNoMovement(this);
        MR::connectToSceneTalkLayoutNoMovement(mAButtonIcon);
    } else {
        MR::connectToSceneLayoutOnPauseMovementCalcAnim(this);
        MR::connectToSceneLayoutOnPauseNoMovement(mAButtonIcon);
    }

    kill();
}

void LuigiLetter::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE(LuigiLetter, HostTypeNrvAppear));
}

void LuigiLetter::movement() {
    LayoutActor::movement();
    mAButtonIcon->movement();
}

void LuigiLetter::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
        MR::startSystemSE("SE_SY_LETTER_APPEAR");
    }

    if (MR::isAnimStopped(this, 0)) {
        MR::startSystemSE("SE_SV_LUIGI_MAIL");
        setNerve(GET_NERVE(LuigiLetter, HostTypeNrvWait));
    }
}

void LuigiLetter::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Wait", 0);
        mAButtonIcon->openWithoutMessage();
    }

    if (mAButtonIcon->isWait() && MR::isHeldJump(PLAYER_1)) {
        MR::startSystemSE("SE_SY_TALK_OK");
        setNerve(GET_NERVE(LuigiLetter, HostTypeNrvEnd));
    }
}

void LuigiLetter::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "End", 0);
        mAButtonIcon->term();
    }

    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}

namespace MR {
    LuigiLetter* createLuigiLetterForTalk() {
        return new LuigiLetter(true, MR::getLuigiLetterGalaxyNameForNPC());
    }

    LuigiLetter* createLuigiLetterForMenu() {
        return new LuigiLetter(false, MR::getLuigiLetterGalaxyName());
    }
};  // namespace MR
