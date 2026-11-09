#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class Karikari;
class FixedPosition;

class KarikariDirector : public LiveActorGroup {
public:
    KarikariDirector(const char*);

    virtual void movement() override;

    bool isMaxNumCling() const;
    void removeAllClingingKarikari();
    void blowOutAllClingingKarikari(const Vec3f&);
    void electricKillAllClingingKarikari();
    void clearScratchInfo();
    bool requestRelease(Karikari*, Vec2f, f32);
    bool registCling(Karikari*);
    bool unregistCling(Karikari*);

    inline MtxPtr getClingMtx(int idx) const {
        return mClingJointMtxs[idx].toMtxPtr();
    }

    s32 mClingNum;
    Pos3f* mClingJointMtxs;  // array
    FixedPosition** mClingPositions;
    Karikari** mClingingKarikari;
    Karikari* mReleaseKarikari;
    Vec2f mReleaseVel;
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
