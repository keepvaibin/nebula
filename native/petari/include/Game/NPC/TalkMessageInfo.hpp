#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class TalkMessageInfo {
public:
    TalkMessageInfo();

    bool isNormalTalk() const;
    bool isShortTalk() const;
    bool isEventTalk() const;
    bool isComposeTalk() const;
    bool isFlowTalk() const;
    bool isNullTalk() const;
    bool isBalloonCall() const;
    bool isBalloonFix() const;
    bool isBalloonSign() const;
    bool isBalloonInfo() const;
    bool isBalloonIcon() const;
    bool isCameraNormal() const;
    bool isCameraEvent() const;

    enum TalkType {
        TalkType_Normal,
        TalkType_Short,
        TalkType_Event,
        TalkType_Compose,
        TalkType_Flow,
        TalkType_Null,
    };

    enum BalloonType {
        BalloonType_Call = 2,
        BalloonType_Fix,
        BalloonType_Sign,
        BalloonType_Info,
        BalloonType_Icon,
    };

    enum CameraType {
        CameraType_Normal,
        CameraType_Event,
        CameraType_None,
    };

    const char16_t* mMessage;
    u16 mCameraSetID;
    s8 mSoundId;
    u8 mCameraType;  // CameraType
    u8 mTalkType;    // TalkType
    u8 mBalloonType;  // BalloonType
    s8 mAreaId;
    s8 mAlreadyReadId;
};
