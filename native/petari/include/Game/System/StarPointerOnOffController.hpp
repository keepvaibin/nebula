#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/Array.hpp"

enum StarPointerModeEnum {
    StarPointerMode_HomeButton = 0,
    StarPointerMode_ErrorWindow = 1,
    StarPointerMode_SaveLoad = 2,
    StarPointerMode_Boot = 3,
    StarPointerMode_Title = 4,
    StarPointerMode_FileSelect = 5,
    StarPointerMode_ScenarioSelectScene = 6,
    StarPointerMode_PauseMenu = 7,
    StarPointerMode_ChooseYesNo = 8,
    StarPointerMode_PictureBook = 9,
    StarPointerMode_DemoWithStarPointer = 10,
    StarPointerMode_DemoWithHandPointerFinger = 11,
    StarPointerMode_Demo = 12,
    StarPointerMode_DemoMarioDeath = 13,
    StarPointerMode_PowerStarGetDemo = 14,
    StarPointerMode_1PInvalid2PValid = 15,
    StarPointerMode_SphereSelectorOnReaction = 16,
    StarPointerMode_SphereSelectorFinger = 17,
    StarPointerMode_MarioLauncher = 18,
    StarPointerMode_CommandStream = 19,
    StarPointerMode_BlueStar = 20,
    StarPointerMode_BlueStarReady = 21,
    StarPointerMode_StarPieceTarget = 22,
    StarPointerMode_BigBubble = 23,
    StarPointerMode_Base = 24,
    StarPointerMode_Game = 25,

    StarPointerMode_Count,
};

class StarPointerOnOffController : public NerveExecutor {
public:
    struct RequestInfo {
    public:
        void* mRequester;
        s32 mMode;
    };

    StarPointerOnOffController();

    void update();
    void setStateToTitle(void*);
    void setStateToFileSelect(void*);
    void setStateToBase(void*);
    void popState(void*);
    void requestMode(void*, StarPointerModeEnum);
    void updateMode();
    void clearInfo();
    void incModeCounter(void*, s32);
    void decModeCounter(void*);

    bool isMode(StarPointerModeEnum mode) {
        return mModeCounter[mode] != 0 || mRequested[mode];
    }

    bool compareMode(StarPointerModeEnum mode) const {
        return mMode == mode;
    }

    MR::InlineArray< RequestInfo*, 16 > mRequests;
    s32 mModeCounter[StarPointerMode_Count];
    bool mRequested[StarPointerMode_Count];
    s32 mMode;
};
