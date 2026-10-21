#include "Game/NPC/RosettaDemoAstroDome.hpp"
#ifdef TARGET_PC
#include "Game/System/NebulaCutsceneSkip.hpp"
#include <nebula_game_api.h>
#endif
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/Rosetta.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstdio>

// TODO: this file is not linkable yet due to a mismatch in .data related to TalkMessageFunc

namespace NrvRosettaDemoAstroDomeExplain {
    NEW_NERVE(RosettaDemoAstroDomeExplainNrvExplainDemo, RosettaDemoAstroDomeExplain, Demo);
};  // namespace NrvRosettaDemoAstroDomeExplain

namespace NrvRosettaDemoAstroDomeFinalBattle {
    NEW_NERVE(RosettaDemoAstroDomeFinalBattleNrvFinalBattleDemo, RosettaDemoAstroDomeFinalBattle, Demo);
};  // namespace NrvRosettaDemoAstroDomeFinalBattle

namespace NrvRosettaDemoAstroDomeTalk {
    NEW_NERVE(RosettaDemoAstroDomeTalkNrvTalkDemo, RosettaDemoAstroDomeTalk, Demo);
};  // namespace NrvRosettaDemoAstroDomeTalk

template < typename T >
static void EntryDemo(T* caller, const char* pDemoName, const char* pRootName, const JMapInfoIter& rIter) {
    if (MR::isDemoExist(pDemoName)) {
        TVec3f offset(MR::getMessageBalloonFollowOffset(caller->mRosetta->mMsgCtrl));
        TalkMessageCtrl* ctrl = MR::createTalkCtrlDirectOnRootNodeAutomatic(caller->mRosetta, rIter, pRootName, offset, nullptr);
        MR::registerEventFunc(ctrl, TalkMessageFunc(caller->mRosetta, &Rosetta::eventFunc));
        DemoFunction::registerDemoTalkMessageCtrlDirect(caller->mRosetta, ctrl, pDemoName);
        MR::registerDemoActionFunctorDirect(caller->mRosetta, MR::Functor(caller, &T::startDemo), pDemoName, "\212\112\216\156" /* 開始 */);
    }
}

RosettaMonologue::RosettaMonologue() : SimpleLayout("\203\215\203\133\203\142\203\136\202\314\214\352\202\350" /* ロゼッタの語り */, "PrologueStarSteward", 2, -1), mTextFormer(this, "Text00") {
    MR::createAndAddPaneCtrl(this, "TalkBalloon", 1);
    MR::createAndAddPaneCtrl(this, "CrossFade1", 1);
    MR::createAndAddPaneCtrl(this, "CrossFade2", 1);
    MR::createAndAddPaneCtrl(this, "CrossFade3", 1);

    mIconAButton = new IconAButton(true, false);
    mIconAButton->initWithoutIter();
    mIconAButton->kill();
    mMessageNo = 0;
}

void RosettaMonologue::appear() {
    LayoutActor::appear();
    mMessageNo = 0;

    MR::startAnim(this, "FadeIn", 0);
    MR::startPaneAnim(this, "TalkBalloon", "WinFadeIn", 0);

    char messageId[256];
    snprintf(messageId, sizeof(messageId), "RosettaMonologue%03d", mMessageNo);
    mTextFormer.formMessage(MR::getGameMessageDirect(messageId), 1);
}

void RosettaMonologue::calcAnim() {
    LayoutActor::calcAnim();
    mIconAButton->calcAnim();
}

void RosettaMonologue::movement() {
    LayoutActor::movement();
    mIconAButton->movement();
}

void RosettaMonologue::control() {
    if (mMessageNo >= 4) {
        if (MR::isAnimStopped(this, 0)) {
            MR::forceCloseWipeFade();
            kill();
        }
        return;
    }

#ifdef TARGET_PC
    // Nebula's text speed, for Rosalina's storybook too.
    mTextFormer.updateTalkingAtTextSpeed();
#else
    mTextFormer.updateTalking();
#endif

    if (!mTextFormer.isTextAppearedAll()) {
        return;
    }

    if (!mIconAButton->isOpen()) {
        mIconAButton->openWithoutMessage();
    }

    MR::setLayoutPosAtPaneTrans(mIconAButton, this, "AButtonPosition");

#ifdef TARGET_PC
    // A skipped reading turns each page as soon as it is up.
    if (!MR::testCorePadTriggerA(WPAD_CHAN0) && !NebulaCutsceneSkip::isSkipping()) {
#else
    if (!MR::testCorePadTriggerA(WPAD_CHAN0)) {
#endif
        return;
    }

    mIconAButton->term();

    if (mTextFormer.nextPage()) {
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
        return;
    }

    mMessageNo++;

    if (mMessageNo == 4) {
        MR::startSystemSE("SE_SY_TALK_OK");
        MR::startCSSound("CS_CLICK_CLOSE", nullptr, WPAD_CHAN0);
    } else {
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");

        char messageId[256];
        snprintf(messageId, sizeof(messageId), "RosettaMonologue%03d", mMessageNo);

        mTextFormer.formMessage(MR::getGameMessageDirect(messageId), 1);
    }

    switch (mMessageNo) {
    case 1:
        MR::startPaneAnim(this, "CrossFade1", "CrossFade1", 0);
        break;
    case 2:
        MR::startPaneAnim(this, "CrossFade2", "CrossFade2", 0);
        break;
    case 3:
        MR::startPaneAnim(this, "CrossFade3", "CrossFade3", 0);
        break;
    case 4:
        MR::startAnim(this, "FadeOut", 0);
        MR::startPaneAnim(this, "TalkBalloon", "WinFadeOut", 0);
        break;
    }
}

RosettaDemoAstroDomeExplain::RosettaDemoAstroDomeExplain(Rosetta* pRosetta, const JMapInfoIter& rIter)
    : NerveExecutor("\203\215\203\133\203\142\203\136\217\363\213\265\220\340\226\276\203\146\203\202\216\300\215\163\216\322" /* ロゼッタ状況説明デモ実行者 */), mRosetta(pRosetta) {
    const char* sDemoExplain = "\203\215\203\133\203\142\203\136\217\363\213\265\220\340\226\276\203\146\203\202" /* ロゼッタ状況説明デモ */;
    DemoFunction::tryCreateDemoTalkAnimCtrlForSceneDirect(mRosetta, sDemoExplain, rIter, "DemoWithButler", nullptr, 0, 0);
    DemoFunction::registerDemoTalkMessageCtrlDirect(mRosetta, mRosetta->mMsgCtrl, sDemoExplain);
    MR::registerDemoActionFunctorDirect(mRosetta, MR::Functor(this, &RosettaDemoAstroDomeExplain::startDemo), sDemoExplain, "\217\363\213\265\220\340\226\276[\212\112\216\156]" /* 状況説明[開始] */);
    mMonologue = new RosettaMonologue();
    initNerve(GET_NERVE(RosettaDemoAstroDomeExplain, RosettaDemoAstroDomeExplainNrvExplainDemo));
}

void RosettaDemoAstroDomeExplain::makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter) {
    pCollector->addArchive("PrologueStarSteward");
}

void RosettaDemoAstroDomeExplain::startDemo() {
    mRosetta->startDemo(this);
}

void RosettaDemoAstroDomeExplain::exeDemo() {
    if (MR::isDemoPartActive("\217\363\213\265\220\340\226\276[\212\107\226\173\225\134\216\246]" /* 状況説明[絵本表示] */)) {
        if (MR::isDemoPartFirstStep("\217\363\213\265\220\340\226\276[\212\107\226\173\225\134\216\246]" /* 状況説明[絵本表示] */)) {
            MR::tryFrameToScreenCinemaFrame();
            mMonologue->appear();
            MR::pauseTimeKeepDemo(mRosetta);
        }

        mMonologue->movement();
        mMonologue->calcAnim();

        if (MR::isDead(mMonologue)) {
            MR::resumeTimeKeepDemo(mRosetta);
        }
    }

    if (MR::isDemoLastStep()) {
        mRosetta->endDemo();
    }
}

RosettaDemoAstroDomeFinalBattle::RosettaDemoAstroDomeFinalBattle(Rosetta* pRosetta, const JMapInfoIter& rIter)
    : NerveExecutor("\203\215\203\133\203\142\203\136\215\305\217\111\214\210\220\355\203\146\203\202\216\300\215\163\216\322" /* ロゼッタ最終決戦デモ実行者 */), mRosetta(pRosetta) {
    EntryDemo(this, "\203\215\203\133\203\142\203\136\215\305\217\111\214\210\220\355\203\146\203\202" /* ロゼッタ最終決戦デモ */, "AstroGalaxy_Rosetta300", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\155\201\133\203\175\203\213\203\107\203\223\203\146\203\102\203\223\203\117\214\343\203\146\203\202" /* ロゼッタノーマルエンディング後デモ */, "AstroGalaxy_Rosetta400", rIter);

    MR::needStageSwitchWriteA(mRosetta, rIter);
    initNerve(GET_NERVE(RosettaDemoAstroDomeFinalBattle, RosettaDemoAstroDomeFinalBattleNrvFinalBattleDemo));
}

void RosettaDemoAstroDomeFinalBattle::startDemo() {
    mRosetta->startDemo(this);
}

void RosettaDemoAstroDomeFinalBattle::exeDemo() {
    if (MR::isDemoLastStep()) {
        mRosetta->endDemoWithInterpole();
    }
}

RosettaDemoAstroDomeTalk::RosettaDemoAstroDomeTalk(Rosetta* pRosetta, const JMapInfoIter& rIter)
    : NerveExecutor("\203\215\203\133\203\142\203\136\211\357\230\142\203\146\203\202\216\300\215\163\216\322" /* ロゼッタ会話デモ実行者 */), mRosetta(pRosetta) {
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\114\203\155\203\163\203\111\222\124\214\237\221\340\203\146\203\202" /* ロゼッタキノピオ探検隊デモ */, "AstroGalaxy_Rosetta080", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\147\201\133\203\140\202\314\211\212\220\340\226\276\203\146\203\202" /* ロゼッタトーチの炎説明デモ */, "AstroGalaxy_Rosetta020", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\122\203\201\203\142\203\147\220\340\226\276\203\146\203\202" /* ロゼッタコメット説明デモ */, "AstroGalaxy_Rosetta030", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\213\342\211\315\202\314\222\206\220\123\220\340\226\276\203\146\203\202" /* ロゼッタ銀河の中心説明デモ */, "AstroGalaxy_Rosetta040", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\223\126\225\266\221\344\213\100\224\134\211\361\225\234\203\146\203\202" /* ロゼッタ天文台機能回復デモ */, "AstroGalaxy_Rosetta050", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\112\203\105\203\223\203\147\203\137\203\105\203\223\212\112\216\156\203\146\203\202" /* ロゼッタカウントダウン開始デモ */, "AstroGalaxy_Rosetta060", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\147\201\133\203\140\202\314\211\212\220\151\222\273\203\146\203\202" /* ロゼッタトーチの炎進捗デモ */, "AstroGalaxy_Rosetta084", rIter);
    EntryDemo(this, "\203\215\203\133\203\142\203\136\203\213\203\103\201\133\203\127\203\146\203\202" /* ロゼッタルイージデモ */, "AstroGalaxy_Rosetta054", rIter);

    initNerve(GET_NERVE(RosettaDemoAstroDomeTalk, RosettaDemoAstroDomeTalkNrvTalkDemo));
}

void RosettaDemoAstroDomeTalk::startDemo() {
    mRosetta->startDemo(this);
}

void RosettaDemoAstroDomeTalk::exeDemo() {
    if (MR::isDemoLastStep()) {
        mRosetta->endDemo();
    }
}
