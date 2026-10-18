#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Util/HashUtil.hpp"

struct MarioAnimationCallback {
    const char* mAnimation;
    s32 mType;
    void (MarioAnimator::*mEntry)();
    void (MarioAnimator::*mUpdate)();
    void (MarioAnimator::*mClose)();
    u32 _2C;
};

MarioAnimationCallback marioCallbackTable[] = {
    {"\213\363\222\206\202\320\202\313\202\350" /* 空中ひねり */, 0, &MarioAnimator::spinEntry, &MarioAnimator::spinUpdate, &MarioAnimator::spinClose, 0},
    {"\222\156\217\343\202\320\202\313\202\350" /* 地上ひねり */, 0, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */, 1, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\101\203\103\203\130\202\320\202\313\202\350\220\303\216\176" /* アイスひねり静止 */, 1, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */, 2, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223\213\363\222\206" /* ファイアスピン空中 */, 2, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */, 3, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\156\203\140\203\130\203\163\203\223\213\363\222\206" /* ハチスピン空中 */, 3, &MarioAnimator::spinEntry, nullptr, &MarioAnimator::spinClose, 0},
    {"\203\130\203\145\201\133\203\127\203\103\203\223A" /* ステージインA */, 0, nullptr, &MarioAnimator::stageInCheck, nullptr, 0},
    {"\223\212\202\260" /* 投げ */, 0, &MarioAnimator::throwEntry, &MarioAnimator::throwCheck, &MarioAnimator::throwClose, 0},
    {"\203\164\203\100\203\103\203\101\223\212\202\260" /* ファイア投げ */, 1, &MarioAnimator::throwEntry, nullptr, &MarioAnimator::throwClose, 0},
    {"\203\124\203\175\201\133\203\134\203\213\203\147" /* サマーソルト */, 0, nullptr, &MarioAnimator::squatSpinCheck, nullptr, 0},
    {"\203\105\203\110\201\133\203\116\203\103\203\223" /* ウォークイン */, 0, nullptr, nullptr, &MarioAnimator::walkinClose, 0},
    {"\214\251\202\351" /* 見る */, 0, nullptr, nullptr, &MarioAnimator::walkinClose, 0},
    {"ResultWait", 0, nullptr, nullptr, &MarioAnimator::walkinClose, 0},
    {"ResultWaitGrandStar", 0, nullptr, nullptr, &MarioAnimator::walkinClose, 0},
    {"WatchUpMore", 0, nullptr, nullptr, &MarioAnimator::walkinClose, 0},
    {"", 0, nullptr, nullptr, nullptr, 0},
};

void MarioAnimator::initCallbackTable() {
    u32 count = 0;
    MarioAnimationCallback* callback = marioCallbackTable;
    for (;; count++, callback++) {
        if (!*callback->mAnimation) {
            break;
        }
    }

    mCallbackTable = new HashSortTable(count);
    callback = marioCallbackTable;
    for (u32 i = 0; i < count; callback++, i++) {
        mCallbackTable->add(callback->mAnimation, i, false);
    }

    mCallbackTable->sort();
    mCallbackId = -1;
    mCallbackEnded = false;
}

void MarioAnimator::entryCallback(const char* pName) {
    mCallbackEnded = false;
    closeCallback();
    u32 index;
    if (mCallbackTable->search(pName, &index)) {
        mCallbackId = index;
        if (marioCallbackTable[mCallbackId].mEntry != nullptr) {
            (this->*marioCallbackTable[mCallbackId].mEntry)();
        }
    }
}

void MarioAnimator::runningCallback() {
    if (mCallbackId == -1) {
        return;
    }

    mCallbackEnded = true;
    if (isAnimationStop() || isAnimationTerminate(nullptr)) {
        closeCallback();
        return;
    }

    mCallbackEnded = false;
    if (!isAnimationRun(marioCallbackTable[mCallbackId].mAnimation)) {
        closeCallback();
        return;
    }

    if (marioCallbackTable[mCallbackId].mUpdate != nullptr) {
        (this->*marioCallbackTable[mCallbackId].mUpdate)();
    }
}

void MarioAnimator::closeCallback() {
    if (mCallbackId != -1 && marioCallbackTable[mCallbackId].mClose != nullptr) {
        (this->*marioCallbackTable[mCallbackId].mClose)();
    }

    mCallbackId = -1;
}

void MarioAnimator::spinEntry() {
    switch (marioCallbackTable[mCallbackId].mType) {
    case 0:
        playEffect("\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);
        break;
    case 1:
        playEffect("\203\101\203\103\203\130\203\130\203\163\203\223" /* アイススピン */);
        break;
    case 2:
        playEffect("\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */);
        break;
    case 3:
        if (gIsLuigi) {
            playEffect("\203\156\203\140\203\213\203\103\201\133\203\127\203\130\203\163\203\223" /* ハチルイージスピン */);
        } else {
            playEffect("\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */);
        }

        break;
    }
}

void MarioAnimator::spinUpdate() {
    if (getFrame() > 30.0f) {
        stopEffect("\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);
    }
}

void MarioAnimator::spinClose() {
    switch (marioCallbackTable[mCallbackId].mType) {
    case 0:
        stopEffect("\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);
        break;
    case 1:
        stopEffect("\203\101\203\103\203\130\203\130\203\163\203\223" /* アイススピン */);
        break;
    case 2:
        stopEffect("\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */);
        break;
    case 3:
        if (gIsLuigi) {
            stopEffect("\203\156\203\140\203\213\203\103\201\133\203\127\203\130\203\163\203\223" /* ハチルイージスピン */);
        } else {
            stopEffect("\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */);
        }

        break;
    }
}

void MarioAnimator::stageInCheck() {
    if (static_cast< s32 >(getFrame()) == 50) {
        Mario* player = getPlayer();
        playEffectRT("\221\256\220\253\203\130\203\145\201\133\203\127\203\103\203\223" /* 属性ステージイン */, player->_368, getTrans());
    }
}

void MarioAnimator::throwCheck() {
    if (mActor->_38C == 0 && getStickP() && !getPlayer()->mMovementStates.jumping) {
        stopAnimation(nullptr);
    }
}

void MarioAnimator::throwEntry() {
    switch (marioCallbackTable[mCallbackId].mType) {
    case 0:
        playEffect("\202\261\202\244\202\347\223\212\202\260" /* こうら投げ */);
        break;
    case 1:
        playEffect("\203\164\203\100\203\103\203\101\203\173\201\133\203\213\223\212\202\260" /* ファイアボール投げ */);
        break;
    }
}

void MarioAnimator::throwClose() {
    switch (marioCallbackTable[mCallbackId].mType) {
    case 0:
        stopEffect("\202\261\202\244\202\347\223\212\202\260" /* こうら投げ */);
        break;
    case 1:
        stopEffect("\203\164\203\100\203\103\203\101\203\173\201\133\203\213\223\212\202\260" /* ファイアボール投げ */);
        break;
    }
}

void MarioAnimator::squatSpinCheck() {
    if (!getPlayer()->mMovementStates._A && getFrame() >= 40.0f) {
        stopAnimation(nullptr);
    }
}

void MarioAnimator::walkinClose() {
    if (mCallbackEnded) {
        stopAnimation(nullptr);
        getPlayer()->changeAnimationInterpoleFrame(16);
    }
}
