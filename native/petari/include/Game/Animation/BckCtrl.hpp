#pragma once

#include "Game/Util/Array.hpp"

class ResourceHolder;
class XanimePlayer;

class BckCtrlData {
public:
    BckCtrlData() : mName(""), mPlayFrame(-1), mStartFrame(-1), mEndFrame(-1), mRepeatFrame(-1), mInterpole(-1), mLoopMode(-1) {
    }

    const char* mName;
    s16 mPlayFrame;
    s16 mStartFrame;
    s16 mEndFrame;
    s16 mRepeatFrame;
    s16 mInterpole;
    u8 mLoopMode;
    u8 _F;
    u8 _10;
    u8 _11;
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
