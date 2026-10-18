#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioStun.hpp"

MarioStun::MarioStun(MarioActor* pActor) : MarioState(pActor, MarioStatus_Stun), _12(), _14() {
}

bool MarioStun::close() {
    stopAnimation("\202\265\202\321\202\352" /* しびれ */);  // "hesitation"
    return true;
}

bool MarioStun::start() {
    changeAnimationNonStop("\202\265\202\321\202\352" /* しびれ */);
    startPadVib("\203\175\203\212\203\111[\202\265\202\321\202\352]" /* マリオ[しびれ] */);
    playSound("\220\272\202\265\202\321\202\352" /* 声しびれ */);
    _14 = 0x3c;
    _12 = 0;
    return true;
}

bool MarioStun::update() {
    if (_14 != 0) {
        _14--;
    }

    if (_14 == 0) {
        if (_12 != 0) {
            return false;
        }

        _12 = 1;
        if (!getPlayer()->mMovementStates._1) {
            _14 = 0xa;
        } else {
            _14 = 0x1e;
        }

        if (getPlayer()->mMovementStates._1) {
            changeAnimation("\202\265\202\321\202\352\211\361\225\234" /* しびれ回復 */);
        }
    }

    if (_12 != 0 && (mActor->isRequestRush() || checkTrgA())) {
        stopAnimation(nullptr);
        if (checkTrgA()) {
            getPlayer()->tryJump();
        }

        return false;
    }

    return true;
}
