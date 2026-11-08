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

    u8* mMessage;
    u16 mCameraSetID;
    s8 mSoundId;
    u8 mCameraType;
    u8 mTalkType;
    u8 mBalloonType;
    s8 mAreaId;
    s8 mAlreadyReadId;
};
