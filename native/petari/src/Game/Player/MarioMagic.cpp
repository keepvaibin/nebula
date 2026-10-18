#include "Game/Player/MarioMagic.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioModule.hpp"

void Mario::stopPunch() {
    if (isStatusActive(MarioStatus_Magic)) {
        closeStatus(mMagic);
    }

    MarioActor* actor = mActor;

    if (!actor->_944) {
        actor->_945 = 0;
        actor->_974 = 0;
    }

    actor->_944 = 0;
}

void Mario::startMagic() {
    if (!mMovementStates.jumping) {
        if (!mActor->_468) {
            if (!mMovementStates._23) {
                if (!isStatusActive(MarioStatus_Slider)) {
                    if (isSkatableFloor()) {
                        doSkate();
                    } else {
                        clearSlope();
                        changeStatus(mMagic);
                        stopAnimationUpper(nullptr);
                        _10._1 = 1;
                    }
                }
            }
        }
    }
}

MarioMagic::MarioMagic(MarioActor* pActor) : MarioState(pActor, MarioStatus_Magic) {
    _12 = 0;
}

bool MarioMagic::start() {
    changeAnimation("\222\156\217\343\202\320\202\313\202\350" /* 地上ひねり */);
    stopEffect("\203\160\203\223\203\140\203\165\203\211\201\133\215\266" /* パンチブラー左 */);
    stopEffect("\203\160\203\223\203\140\203\165\203\211\201\133\211\105" /* パンチブラー右 */);
    playEffect("\213\244\222\312\222\156\217\343\203\130\203\163\203\223" /* 共通地上スピン */);
    playSound("\220\272\203\130\203\163\203\223" /* 声スピン */);
    playSound("\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* スピンジャンプ */);
    startPadVib(2);
    _12 = 0;
    return true;
}

bool MarioMagic::close() {
    stopEffect("\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);

    if (_12 < 0x1A) {
        playEffect("\203\130\203\163\203\223\203\211\203\103\203\147\217\301\213\216" /* スピンライト消去 */);
    }

    return true;
}

bool MarioMagic::update() {
    if (mActor->isRequestJump()) {
        getPlayer()->tryJump();
        return false;
    } else if (!isAnimationRun("\222\156\217\343\202\320\202\313\202\350" /* 地上ひねり */)) {
        return false;
    }

    _12++;

    if (_12 == 25) {
        stopEffect("\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);
        playEffect("\203\130\203\163\203\223\203\211\203\103\203\147\217\301\213\216" /* スピンライト消去 */);
    }

    if (getPlayer()->mMovementStates.jumping) {
        getPlayer()->procJump(false);
    } else {
        getPlayer()->mainMove();
    }

    getPlayer()->updateWalkSpeed();
    return true;
}
