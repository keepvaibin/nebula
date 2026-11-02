#pragma once


class SyncBckEffectInfo;
class XanimePlayer;

class SyncBckEffectChecker {
public:
    SyncBckEffectChecker(XanimePlayer* pPlayer);

    void updateBefore();
    void updateAfter();
    void reset();
    bool isCreate(const SyncBckEffectInfo* pInfo, bool checkFrame) const;
    bool isDelete(const SyncBckEffectInfo* pInfo) const;
    bool checkPass(f32 frame) const;
    bool checkPassIfRate0(f32 frame) const;

    XanimePlayer* mPlayer;
    f32 mPrevFrame;
    bool mIsReset;
    const char* mCurrentBckName;
    const char* mPrevBckName;
};
