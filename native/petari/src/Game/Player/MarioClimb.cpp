#include "Game/Player/MarioClimb.hpp"
#include "Game/Player/MarioActor.hpp"

void Mario::connectToClimb() {
    _790 = -getShadowNorm();
    changeStatus(mClimb);
}

MarioClimb::MarioClimb(MarioActor* pActor) : MarioState(pActor, MarioStatus_Climb), mTimer() {
}

bool MarioClimb::update() {
    if (mTimer < 15 && checkTrgA()) {
        getPlayer()->tryJump();
        return false;
    }

    if (mTimer != 0) {
        mTimer--;
    }

    addVelocity(getFrontVec(), 6.0f);

    if (mTimer == 0) {
        return false;
    }

    return true;
}

bool MarioClimb::start() {
    changeAnimation("\231\263\231\264\221\117\220\151" /* 匍匐前進 */, "\231\263\231\264\221\117\220\151" /* 匍匐前進 */);

    if (mActor->_468 != 0) {
        changeAnimationUpper("\202\320\202\353\202\242\203\105\203\107\203\103\203\147" /* ひろいウエイト */);
    }

    mTimer = 15;
    return true;
}

bool MarioClimb::close() {
    getPlayer()->mWalkSpeed = getStickP();

    if (getPlayer()->getMovementStates()._1) {
        stopAnimation("\231\263\231\264\221\117\220\151" /* 匍匐前進 */, "\212\356\226\173" /* 基本 */);
    } else {
        stopAnimation("\231\263\231\264\221\117\220\151" /* 匍匐前進 */, "\227\216\211\272" /* 落下 */);
        getPlayer()->set3BC(8);
    }

    return true;
}
