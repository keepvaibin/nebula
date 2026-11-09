#include "Game/System/StarPointerOnOffController.hpp"
#include "Game/System/NebulaInput.hpp"
#include <nebula_game_api.h>
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Screen/StarPointerLayout.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/NebulaHost.hpp"

namespace {
    inline StarPointerDirector* getStarPointerDirector() {
        return StarPointerFunction::getStarPointerDirector();
    }

    inline StarPointerLayout* getStarPointerLayout(s32 channel) {
        return getStarPointerDirector()->getStarPointerLayout(channel);
    }

    enum PointerKind {
        Pointer_Keep,
        Pointer_Star,
        Pointer_Finger,
        Pointer_Hand,
        Pointer_HandCross,
        Pointer_Nozzle,
    };

    // A mode's pointers: each player's shown and valid, the pointer kind (Keep leaves it), guidance, star bit shooting,
    // and 2P shooting on whatever shooting says.
    struct ModeSetup {
        bool mIsShown1P;
        bool mIsShown2P;
        u8 mPointer;
        bool mIsValid1P;
        bool mIsValid2P;
        bool mIsGuidance;
        bool mIsShoot;
        bool mIsShoot2P;
    };

    // By StarPointerModeEnum. 1PInvalid2PValid validates both; the mode is read by isStarPointer1PInvalid2PValidMode.
    const ModeSetup cModeSetups[StarPointerMode_Count] = {
        {false, false, Pointer_Keep, true, true, false, false, false},       // HomeButton
        {false, false, Pointer_Keep, true, true, false, false, false},       // ErrorWindow
        {true, true, Pointer_Finger, true, true, false, false, false},       // SaveLoad (as PauseMenu)
        {false, false, Pointer_Star, false, false, false, false, false},     // Boot
        {false, false, Pointer_Star, false, false, false, false, false},     // Title
        {true, true, Pointer_Finger, true, true, true, false, false},        // FileSelect
        {true, true, Pointer_Finger, true, true, true, false, false},        // ScenarioSelectScene
        {true, true, Pointer_Finger, true, true, false, false, false},       // PauseMenu
        {true, true, Pointer_Finger, true, true, true, false, false},        // ChooseYesNo
        {true, true, Pointer_Finger, true, false, false, false, false},      // PictureBook
        {true, true, Pointer_Star, true, true, true, false, false},          // DemoWithStarPointer
        {true, true, Pointer_Finger, true, true, true, false, false},        // DemoWithHandPointerFinger
        {false, false, Pointer_Star, false, false, false, false, false},     // Demo
        {true, true, Pointer_Star, false, false, true, false, false},        // DemoMarioDeath
        {false, false, Pointer_Star, false, false, false, false, false},     // PowerStarGetDemo
        {true, true, Pointer_Star, true, true, false, true, false},          // 1PInvalid2PValid
        {true, true, Pointer_HandCross, true, true, true, false, false},     // SphereSelectorOnReaction
        {true, true, Pointer_Finger, true, true, true, false, false},        // SphereSelectorFinger
        {false, true, Pointer_Finger, false, false, true, false, false},     // MarioLauncher
        {true, true, Pointer_Star, true, true, true, false, true},           // CommandStream
        {true, true, Pointer_Hand, true, true, true, true, false},           // BlueStar
        {true, true, Pointer_Hand, true, true, true, true, false},           // BlueStarReady
        {false, false, Pointer_Finger, true, true, true, false, false},      // StarPieceTarget
        {true, true, Pointer_Nozzle, true, true, true, false, true},         // BigBubble
        {false, false, Pointer_Star, false, false, false, false, false},     // Base
        {true, true, Pointer_Star, true, true, true, true, false},           // Game
    };

    void showOrHide(StarPointerLayout* pLayout, bool isShown) {
        if (isShown) {
            pLayout->show();
        } else {
            pLayout->hide();
        }
    }

    void validateOrNot(StarPointerLayout* pLayout, bool isValid) {
        if (isValid) {
            pLayout->validate();
        } else {
            pLayout->invalidate();
        }
    }

    void applyModeSetup(s32 mode) {
        const ModeSetup& rSetup = cModeSetups[mode];
        StarPointerDirector* director = ::getStarPointerDirector();
        showOrHide(::getStarPointerLayout(PLAYER_1), rSetup.mIsShown1P);
        showOrHide(::getStarPointerLayout(PLAYER_2), rSetup.mIsShown2P);

        switch (rSetup.mPointer) {
        case Pointer_Star:
            director->startStarPointer();
            break;
        case Pointer_Finger:
            director->startFingerPointer();
            break;
        case Pointer_Hand:
            director->startHandPointer();
            break;
        case Pointer_HandCross:
            director->startHandPointerReactionWithCrossCursor();
            break;
        case Pointer_Nozzle:
            director->startStarPointerNozzle();
            break;
        default:
            break;
        }

        validateOrNot(::getStarPointerLayout(PLAYER_1), rSetup.mIsValid1P);
        validateOrNot(::getStarPointerLayout(PLAYER_2), rSetup.mIsValid2P);

        if (rSetup.mIsGuidance) {
            MR::activeStarPointerGuidance();
        } else {
            MR::deactiveStarPointerGuidance();
        }

        if (rSetup.mIsShoot) {
            MR::enableStarPointerShootStarPiece();
        } else {
            MR::disableStarPointerShootStarPiece();
        }

        if (rSetup.mIsShoot2P) {
            director->mIsAllowP2StarPieceShot = true;
        }
    }
};  // namespace

StarPointerOnOffController::StarPointerOnOffController() : NerveExecutor("スターポインタON/OFF制御"), mMode(-1) {
    for (s32 idx = 0; idx < 16; idx++) {
        mRequests[idx] = new RequestInfo();
    }

    for (RequestInfo** request = mRequests.begin(); request < mRequests.end(); request++) {
        (*request)->mRequester = nullptr;
        (*request)->mMode = -1;
    }

    for (s32 idx = 0; idx < StarPointerMode_Count; idx++) {
        mModeCounter[idx] = 0;
        mRequested[idx] = false;
    }
}

void StarPointerOnOffController::update() {
    updateMode();
    for (s32 idx = 0; idx < StarPointerMode_Count; idx++) {
        mRequested[idx] = false;
    }
}

void StarPointerOnOffController::setStateToTitle(void* pRequester) {
    clearInfo();
    incModeCounter(pRequester, StarPointerMode_Title);
}

void StarPointerOnOffController::setStateToFileSelect(void* pRequester) {
    clearInfo();
    incModeCounter(pRequester, StarPointerMode_FileSelect);
}

void StarPointerOnOffController::setStateToBase(void* /*pRequester*/) {
    clearInfo();
}

void StarPointerOnOffController::popState(void* pRequester) {
    decModeCounter(pRequester);
}

void StarPointerOnOffController::requestMode(void* /*pRequester*/, StarPointerModeEnum mode) {
    mRequested[mode] = true;
}

void StarPointerOnOffController::updateMode() {
    if (::getStarPointerLayout(PLAYER_1) == nullptr) {
        return;
    }

    updateNerve();

    for (s32 mode = 0; mode < StarPointerMode_Count; mode++) {
        if (mModeCounter[mode] > 0 || mRequested[mode]) {
            if (mode != mMode) {
                mMode = mode;
                NebulaHost::gameContext(NEBULA_CONTEXT_STAR_POINTER_MODE, mMode);
                ::applyModeSetup(mMode);
            }
            return;
        }
    }

    mMode = -1;
    NebulaHost::gameContext(NEBULA_CONTEXT_STAR_POINTER_MODE, mMode);
    ::applyModeSetup(StarPointerMode_Base);
}

void StarPointerOnOffController::clearInfo() {
    for (s32 mode = 0; mode < StarPointerMode_Count; mode++) {
        mModeCounter[mode] = 0;
    }

    for (RequestInfo** request = mRequests.begin(); request < mRequests.end(); request++) {
        (*request)->mRequester = nullptr;
        (*request)->mMode = -1;
    }
}

void StarPointerOnOffController::incModeCounter(void* pRequester, s32 mode) {
    if (mode == -1) {
        return;
    }

    RequestInfo* pFree = nullptr;
    for (RequestInfo** request = mRequests.begin(); request < mRequests.end(); request++) {
        if ((*request)->mRequester == nullptr) {
            pFree = *request;
            break;
        }
    }

    if (pFree == nullptr) {
        // Every slot is taken: the list grows, so a request is never dropped.
        const s32 used = mRequests.size();
        mRequests.grow();
        for (s32 idx = used; idx < mRequests.size(); idx++) {
            mRequests[idx] = new RequestInfo();
            mRequests[idx]->mRequester = nullptr;
            mRequests[idx]->mMode = -1;
        }
        pFree = mRequests[used];
    }

    if (mode == StarPointerMode_DemoMarioDeath && mModeCounter[mode] != 0) {
        return;
    }

    pFree->mRequester = pRequester;
    pFree->mMode = mode;

    mModeCounter[mode]++;
    updateMode();
}

void StarPointerOnOffController::decModeCounter(void* pRequester) {
    for (RequestInfo** request = mRequests.end() - 1; request >= mRequests.begin(); request--) {
        // A free slot (mode -1) is never a request to end, even for a null requester.
        if ((*request)->mRequester == pRequester && (*request)->mMode >= 0) {
            mModeCounter[(*request)->mMode]--;
            for (; request < mRequests.end() - 1; request++) {
                *(*request) = *(*(request + 1));
                (*(request + 1))->mRequester = nullptr;
                (*(request + 1))->mMode = -1;
            }
            updateMode();
            return;
        }
    }
}
