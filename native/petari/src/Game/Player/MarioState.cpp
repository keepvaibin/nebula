#include "Game/Player/MarioState.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"

MarioState::MarioState(MarioActor* pActor, u32 statusId) : MarioModule(pActor), mNextState(), mStatusId(statusId), mClosing() {
}

bool MarioState::proc(u32 msg) {
    switch (msg) {
    case MarioStateMsg_Start:
        return start();
    case MarioStateMsg_Close:
        if (mClosing) {
            break;
        }

        mClosing = true;
        close();
        mClosing = false;
        break;
    case MarioStateMsg_Update:
        return update();
    case MarioStateMsg_Notice:
        return notice();
    case MarioStateMsg_Keep:
        return keep();
    }

    return true;
}

void Mario::sendStateMsg(u32 msg) {
    MarioState* pNext;

    for (MarioState* pState = mCurrentState; pState != nullptr;) {
        pNext = pState->mNextState;

        if (isStatusActive(pState->mStatusId) == MarioStatus_None) {
            pState = pNext;
            continue;
        }

        if (!pState->proc(msg)) {
            if (isStatusActive(pState->mStatusId) != MarioStatus_None) {
                closeStatus(pState);
            }
        } else if (msg == MarioStateMsg_Update) {
            msg = MarioStateMsg_Keep;
        }

        pState = pNext;
    }
}

bool Mario::updatePosture(MtxPtr mtx) {
    if (mCurrentState != nullptr) {
        return mCurrentState->postureCtrl(mtx);
    }

    postureCtrl(mtx);

    return false;
}

bool MarioState::postureCtrl(MtxPtr mtx) {
    getPlayer()->postureCtrl(mtx);

    return false;
}

void Mario::changeStatus(MarioState* pState) {
    if (isStatusActive(pState->mStatusId)) {
        return;
    }

    if (!mActor->isExtraPlayer()) {
        NebulaMods::fire(NEBULA_MOD_EVENT_PLAYER_ACTION, static_cast< s32 >(pState->mStatusId));
    }

    mNoticedState = pState;

    sendStateMsg(MarioStateMsg_Notice);

    if (mCurrentState) {
        pState->mNextState = mCurrentState;
    }

    mCurrentState = pState;

    if (!pState->proc(MarioStateMsg_Start)) {
        closeStatus(pState);

        mCurrentState = pState->mNextState;
    }
}

void Mario::closeStatus(MarioState* pState) {
    if (pState == nullptr) {
        while (mCurrentState != nullptr) {
            closeStatus(mCurrentState);
        }

        return;
    }

    MarioState* pCurr = mCurrentState;

    if (pCurr == pState) {
        mCurrentState = pState->mNextState;
    } else {
        while (pCurr != nullptr) {
            MarioState* pNext = pCurr->mNextState;

            if (pNext == pState) {
                break;
            }

            pCurr = pNext;
        }

        if (pCurr == nullptr) {
            return;  // not open
        }

        pCurr->mNextState = pState->mNextState;
    }

    pState->mNextState = nullptr;
    pState->proc(MarioStateMsg_Close);
}

u32 Mario::getCurrentStatus() const {
    MarioState* pState = mCurrentState;

    if (pState == nullptr) {
        return MarioStatus_None;
    }

    return pState->mStatusId;
}

bool Mario::isStatusActive(u32 statusId) const {
    MarioState* pState = mCurrentState;

    if (pState == nullptr) {
        return false;
    }

    if (statusId == pState->mStatusId) {
        return true;
    }

    while (pState != nullptr) {
        if (statusId == pState->mStatusId) {
            return true;
        }

        pState = pState->mNextState;
    }

    return false;
}

u32 MarioState::getNoticedStatus() const {
    return getPlayer()->mNoticedState->mStatusId;
}
