#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"

TalkMessageInfo::TalkMessageInfo()
    : mMessage(nullptr), mCameraSetID(0), mSoundId(0), mCameraType(CameraType_None), mTalkType(TalkType_Normal), mBalloonType(0), mAreaId(-1),
      mAlreadyReadId(-1) {
}

bool TalkMessageInfo::isNormalTalk() const {
    return mTalkType == TalkType_Normal;
}

bool TalkMessageInfo::isShortTalk() const {
    return mTalkType == TalkType_Short;
}

bool TalkMessageInfo::isEventTalk() const {
    return mTalkType == TalkType_Event;
}

bool TalkMessageInfo::isComposeTalk() const {
    return mTalkType == TalkType_Compose;
}

bool TalkMessageInfo::isFlowTalk() const {
    return mTalkType == TalkType_Flow;
}

bool TalkMessageInfo::isNullTalk() const {
    return mTalkType == TalkType_Null;
}

bool TalkMessageInfo::isBalloonCall() const {
    return mBalloonType == BalloonType_Call;
}

bool TalkMessageInfo::isBalloonFix() const {
    return mBalloonType == BalloonType_Fix;
}

bool TalkMessageInfo::isBalloonSign() const {
    return mBalloonType == BalloonType_Sign;
}

bool TalkMessageInfo::isBalloonInfo() const {
    return mBalloonType == BalloonType_Info;
}

bool TalkMessageInfo::isBalloonIcon() const {
    return mBalloonType == BalloonType_Icon;
}

bool TalkMessageInfo::isCameraNormal() const {
    return mCameraType == CameraType_Normal;
}

bool TalkMessageInfo::isCameraEvent() const {
    return mCameraType == CameraType_Event;
}
