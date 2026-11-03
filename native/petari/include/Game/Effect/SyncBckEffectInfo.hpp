#pragma once


class XanimePlayer;
class J3DAnmTransform;

class SyncBckEffectInfo {
public:
    class BckResourceInfo {
    public:
        BckResourceInfo(const XanimePlayer* pPlayer, const char* pName);
        bool isLoop() const;

        const char* mName;
        J3DAnmTransform* mResource;
    };

    SyncBckEffectInfo(const XanimePlayer* pPlayer, const char* pName, s32 capacity, f32 startFrame, f32 endFrame, bool continueAnimEnd);
    void addBck(const XanimePlayer* pPlayer, const char* pName);
    bool isRegisteredBck(const char* pName) const;
    bool isBckLoop(const char* pName) const;

    BckResourceInfo** mBckResources;
    s32 mCapacity;
    s32 mCount;
    f32 mStartFrame;
    f32 mEndFrame;
    bool mContinueAnimEnd;
};

namespace MR {
    namespace Effect {
        bool isExistSyncBckDeleteFrame(const SyncBckEffectInfo* pInfo);
    };
};  // namespace MR
