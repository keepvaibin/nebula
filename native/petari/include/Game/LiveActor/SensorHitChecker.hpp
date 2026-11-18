#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class SensorGroup;
class HitSensor;
class LiveActor;
struct SensorHitEntry;

class SensorHitChecker : public NameObj {
public:
    SensorHitChecker(const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void initGroup(HitSensor*);
    void doObjColGroup(SensorGroup*, SensorGroup*) const;
    void doObjColInSameGroup(SensorGroup*) const;

    void checkAttack(HitSensor*, HitSensor*) const;

#if defined(NEBULA_PLAIN_PATHS)
    void testPair(const SensorHitEntry*, s32, const SensorHitEntry*, s32) const;
    void addFoundPairs(const SensorHitEntry*, const SensorHitEntry*) const;
#endif

    SensorGroup* mPlayerGroup;
    SensorGroup* mRideGroup;
    SensorGroup* mEyeGroup;
    SensorGroup* mSimpleGroup;
    SensorGroup* mMapObjGroup;
    SensorGroup* mCharacterGroup;
    // The plain module's pairs that touched in one group pair, as (first index << 32 | second index), until their calls.
    mutable MR::GrowBuffer< u64 > mFound;
    mutable u32 mFoundCount;
};

// A sensor as the pair test reads it: its place and radius beside it, so the pairs walk one array instead of each
// sensor and its actor once per partner.
struct SensorHitEntry {
    f32 mX, mY, mZ, mRadius;
    const LiveActor* mHost;
    HitSensor* mSensor;
};

// A placed active sensor's extent along x (its sphere's, a little wider), for the sweep.
struct SensorSweepItem {
    f64 mLow;
    f64 mHigh;
    s32 mIndex;  // in mActive
};

class SensorGroup {
public:
    explicit SensorGroup(int maxSensors);

    void add(HitSensor*);
    void remove(HitSensor*);
    void clear() const;
    void gatherActive();
    void layOutSweep();

    s32 mMaxSensors;
    s32 mSensorCount;
    HitSensor** mSensors;
    // This frame's active sensors (valid, host not clipped), in mSensors' order; mMaxSensors entries, made and grown
    // with mSensors in their arena. Null in the module with hook points, which tests the pairs as the game does.
    s32 mActiveCount;
    SensorHitEntry* mActive;
    // The active sensors with a finite place and radius, by where their extent starts; and the others' places in
    // mActive. Made and grown with mActive; laid out on a frame's first sweep of the group (mSweepMade).
    bool mSweepMade;
    s32 mSweepCount;
    SensorSweepItem* mSweep;
    s32 mUnplacedCount;
    s32* mUnplaced;
};

namespace MR {
    void initHitSensorGroup(HitSensor*);
};  // namespace MR
