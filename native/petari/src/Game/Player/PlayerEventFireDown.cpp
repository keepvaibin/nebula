#include "Game/Player/PlayerEventFireDown.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

EventFireDown::EventFireDown() : EventSequence(16) {
    addEventOnTime("\217\211\212\372\211\273" /* 初期化 */, static_cast< EventFunc1 >(&EventFireDown::init), 0);
    addEventOnTime("\222\312\217\355\203\214\203\103\203\101\203\105\203\147\217\301\213\216" /* 通常レイアウト消去 */, static_cast< EventFunc1 >(&EventFireDown::closeDefaultLayout), 100);
    addEventOnTime("\203\124\203\105\203\223\203\150A" /* サウンドA */, static_cast< EventFunc1 >(&EventFireDown::sound), 30);
    addEventOnTime("\203\124\203\105\203\223\203\150B" /* サウンドB */, static_cast< EventFunc1 >(&EventFireDown::sound2), 50);
    addEventInStatus("\203\217\203\103\203\166\212\112\216\156" /* ワイプ開始 */, static_cast< EventFunc1 >(&EventFireDown::doCloseWipe),
                     static_cast< EventFunc2 >(&EventSequence::isMissLayoutClosed));
    addEventOnTime("\216\143\213\100\202\360\210\370\202\255" /* 残機を引く */, static_cast< EventFunc1 >(&EventFireDown::decLeft), 120);
    addEventInPhase("\203\217\203\103\203\166\217\111\227\271\214\343" /* ワイプ終了後 */, static_cast< EventFunc1 >(&EventFireDown::doWaitAfterWipe), 2);
}

void EventFireDown::init(u16 eventFrame, u16 sequenceFrame) {
    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::stopStageBGM(10);
    MR::stopSubBGM(10);
    MarioAccess::getPlayerActor()->changeGameOverAnimation();
    playSound("\220\272\215\305\217\111\203\137\203\201\201\133\203\127" /* 声最終ダメージ */);
    playSound("\215\305\214\343\202\314\210\352\214\202" /* 最後の一撃 */);
    _28 = 20.0f;
    MR::startPlayerDownWipe();
    MR::startMissLayout();
}

void EventFireDown::sound(u16 eventFrame, u16 sequenceFrame) {
    MR::setSoundVolumeSetting(2, 20);
    MR::startSubBGM("BGM_MISS", false);
}

void EventFireDown::updateAfter() {
    setSpot(_28, _24);
}

void EventFireDown::sound2(u16 eventFrame, u16 sequenceFrame) {
    MR::setSoundVolumeSetting(7, 70);
}
