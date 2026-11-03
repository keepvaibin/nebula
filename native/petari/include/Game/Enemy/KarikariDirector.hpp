#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Karikari;
class FixedPosition;

class KarikariDirector : public LiveActorGroup {
public:
    KarikariDirector(const char*);

    virtual void movement();

    bool isMaxNumCling() const;
    void removeAllClingingKarikari();
    void blowOutAllClingingKarikari(const TVec3f&);
    void electricKillAllClingingKarikari();
    void clearScratchInfo();
    bool requestRelease(Karikari*, TVec2f, f32);
    bool registCling(Karikari*);
    bool unregistCling(Karikari*);

    inline MtxPtr getClingMtx(int idx) const {
        return (MtxPtr)&mClingJointMtxs[idx];
    }

    s32 mClingNum;
    TPos3f* mClingJointMtxs;  // array
    FixedPosition** mClingPositions;
    Karikari** mClingingKarikari;
    Karikari* mReleaseKarikari;
    TVec2f mReleaseVel;
    f32 mLowestReleaseDist;
    s32 mNoReleaseTime;
    s32 mRumbleTime;
};

struct ClingData {
    const char* mJointName;
    Vec mPos;
    Vec mRotDegree;
};

namespace MR {
    s32 getKarikariClingNum();
    s32 getClingNumMax();
    void removeAllClingingKarikari();
};  // namespace MR
