#include "Game/Player/MarioFlow.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::doFlow() {
    if (isStatusActive(MarioStatus_Flow)) {
        return false;
    }

    if (isAnimationRun("\213\363\222\206\202\320\202\313\202\350" /* 空中ひねり */)) {
        return false;
    }

    changeStatus(mFlow);

    return true;
}

bool MarioFlow::start() {
    _12 = 0;
    _14 = 0;

    changeAnimationNonStop("\222\206\203\137\203\201\201\133\203\127" /* 中ダメージ */);
    startPadVib(2);

    getPlayer()->mMovementStates._1 = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates._B = false;
    getPlayer()->mMovementStates._2B = true;

    _18 = getFrontVec() * mActor->mConst->getTable()->mJumpDistFlow;
    _18 += -mActor->_240 * mActor->mConst->getTable()->mJumpHeightFlow;
    getPlayer()->mJumpVec = _18;

    addVelocity(_18);

    getPlayer()->stopWalk();

    return true;
}

bool MarioFlow::update() {
    _12++;

    switch (_14) {
    case 0:
        addVelocity(_18);

        _18 += mActor->_240 * mActor->getConst().getTable()->mGravityFlow;

        if (_12 == 20) {
            changeAnimation("\222\206\203\137\203\201\201\133\203\127\213\363\222\206" /* 中ダメージ空中 */);
        }

        if (_12 > 30 && mActor->isRequestRush()) {
            getPlayer()->trySpinJump(0);

            return false;
        }

        if (getPlayer()->getMovementStates()._1) {
            getPlayer()->mMovementStates.jumping = false;

            changeAnimation("\222\206\203\137\203\201\201\133\203\127\222\205\222\156" /* 中ダメージ着地 */);

            playEffect("\213\244\222\312\203\137\203\201\201\133\203\127\222\205\222\156" /* 共通ダメージ着地 */);
            playSound("\220\201\202\301\224\362\202\321\223\174\202\352" /* 吹っ飛び倒れ */);

            MR::vecKillElement(_18, mActor->_240, &_18);

            _12 = 0;
            _14++;
        }
        break;
    case 1:
        if (getPlayer()->getMovementStates()._1 == false) {
            getPlayer()->mMovementStates.jumping = true;
            _14 = 0;
        } else {
            addVelocity(_18);

            _18.x *= 0.95f;
            _18.y *= 0.95f;
            _18.z *= 0.95f;

            if (!isAnimationRun("\222\206\203\137\203\201\201\133\203\127\222\205\222\156" /* 中ダメージ着地 */)) {
                return false;
            }

            if (_12 > 15 && checkTrgA()) {
                getPlayer()->tryJump();

                return false;
            }
        }
        break;
    }

    getPlayer()->mJumpVec = _18;

    return true;
}

bool MarioFlow::close() {
    stopAnimation("\203\137\203\201\201\133\203\127" /* ダメージ */);
    stopAnimation("\203\137\203\201\201\133\203\127\222\205\222\156" /* ダメージ着地 */, "\212\356\226\173" /* 基本 */);

    return true;
}

MarioFlow::MarioFlow(MarioActor* pActor) : MarioState(pActor, MarioStatus_Flow), _12(), _14() {
    _18.zero();
}
