#pragma once

#include "Game/Util/Array.hpp"

class ResourceHolder;
class XanimePlayer;

class BckCtrlData {
public:
    BckCtrlData() : mName(""), mPlayFrame(-1), mStartFrame(-1), mEndFrame(-1), mRepeatFrame(-1), mInterpole(-1), mLoopMode(-1) {
    }

    const char* mName;
    s32 mPlayFrame;
    s32 mStartFrame;
    s32 mEndFrame;
    s32 mRepeatFrame;
    s32 mInterpole;
    u8 mLoopMode;
};

class BckCtrlFunction {
public:
    static void reflectBckCtrlData(const BckCtrlData&, XanimePlayer*);
};

class BckCtrl {
public:
    BckCtrl(ResourceHolder*, const char*);

    BckCtrlData* find(const char*) const;
    void add(const BckCtrlData&);
    void overWrite(const BckCtrlData&);
    void changeBckSetting(const char*, XanimePlayer*) const;

    BckCtrlData mDefaultCtrlData;
    MR::Vector< MR::AssignableArray< BckCtrlData > > mControlData;
};
