#include "Game/Player/PlayerEventDown.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

EventDown::EventDown() : EventSequence(16) {
    addEventOnTime("\217\211\212\372\211\273" /* 初期化 */, static_cast< EventFunc1 >(&EventDown::init), 0);
    addEventOnTime("MISS\203\214\203\103\203\101\203\105\203\147\212\112\216\156" /* MISSレイアウト開始 */, static_cast< EventFunc1 >(&EventDown::missLayoutOpen), 45);
    addEventOnTime("\222\312\217\355\203\214\203\103\203\101\203\105\203\147\217\301\213\216" /* 通常レイアウト消去 */, static_cast< EventFunc1 >(&EventDown::closeDefaultLayout), 30);
    addEventOnTime("\203\124\203\105\203\223\203\150A" /* サウンドA */, static_cast< EventFunc1 >(&EventDown::sound), 30);
    addEventInTime("\203\217\203\103\203\166\212\112\216\156" /* ワイプ開始 */, static_cast< EventFunc1 >(&EventDown::doCloseWipe), 100, 600);
    addEventOnTime("\216\143\213\100\202\360\210\370\202\255" /* 残機を引く */, static_cast< EventFunc1 >(&EventDown::decLeft), 120);
    addEventInPhase("\203\217\203\103\203\166\217\111\227\271\214\343" /* ワイプ終了後 */, static_cast< EventFunc1 >(&EventSequence::doWaitAfterWipe), 2);
}

void EventDown::init(u16 eventFrame, u16 sequenceFrame) {
    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::stopStageBGM(10);
    MR::stopSubBGM(10);
    MarioAccess::getPlayerActor()->changeGameOverAnimation();
    playSound("\220\272\215\305\217\111\203\137\203\201\201\133\203\127" /* 声最終ダメージ */);
    playSound("\215\305\214\343\202\314\210\352\214\202" /* 最後の一撃 */);
    MR::startPlayerDownWipe();
}
void EventDown::sound(u16 eventFrame, u16 sequenceFrame) {
    switch (sequenceFrame) {
    case 30:
        MR::setSoundVolumeSetting(2, 20);
        MR::startSubBGM("BGM_MISS", false);
        break;
    }
}
void EventDown::missLayoutOpen(u16 eventFrame, u16 sequenceFrame) {
    MR::startMissLayout();
    nextPhase();
}
