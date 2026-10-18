#include "Game/Player/PlayerEventGameOver.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

EventGameOver::EventGameOver() : EventSequence(16) {
    addEventOnTime("\217\211\212\372\211\273" /* 初期化 */, static_cast< EventFunc1 >(&EventGameOver::init), 0);
    addEventOnTime("\222\312\217\355\203\214\203\103\203\101\203\105\203\147\217\301\213\216" /* 通常レイアウト消去 */, static_cast< EventFunc1 >(&EventGameOver::closeDefaultLayout), 60);
    addEventOnTime("\203\124\203\105\203\223\203\150A" /* サウンドA */, static_cast< EventFunc1 >(&EventGameOver::sound), 90);
    addEventOnTime("\203\214\203\103\203\101\203\105\203\147\212\112\216\156" /* レイアウト開始 */, static_cast< EventFunc1 >(&EventGameOver::reqLayout), 160);
    addEventOnTime("\217\111\227\271" /* 終了 */, static_cast< EventFunc1 >(&EventGameOver::finish), 420);
}

void EventGameOver::init(u16 eventFrame, u16 sequenceFrame) {
    MR::requestStartGameOverDemo();
    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::stopStageBGM(10);
    MR::stopSubBGM(10);
    playSound("\220\272\215\305\217\111\203\137\203\201\201\133\203\127" /* 声最終ダメージ */);
    playSound("\215\305\214\343\202\314\210\352\214\202" /* 最後の一撃 */);
    _28 = 20.0f;
}

void EventGameOver::sound(u16 eventFrame, u16 sequenceFrame) {
    switch (sequenceFrame) {
    case 90:
        MR::setSoundVolumeSetting(2, 30);
        MR::startSubBGM("BGM_GAMEOVER", false);
        break;
    }
}

void EventGameOver::updateAfter() {
}

void EventGameOver::reqLayout(u16 eventFrame, u16 sequenceFrame) {
    MR::startGameOverWipe();
}

void EventGameOver::finish(u16 eventFrame, u16 sequenceFrame) {
    MR::requestEndGameOverDemo();
    stopSequence();
}
