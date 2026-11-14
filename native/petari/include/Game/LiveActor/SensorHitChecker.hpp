#pragma once

#include "Game/NameObj/NameObj.hpp"

class SensorGroup;
class HitSensor;
class LiveActor;

class SensorHitChecker : public NameObj {
public:
    SensorHitChecker(const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void initGroup(HitSensor*);
    void doObjColGroup(SensorGroup*, SensorGroup*) const;
    void doObjColInSameGroup(SensorGroup*) const;

    void checkAttack(HitSensor*, HitSensor*) const;

    SensorGroup* mPlayerGroup;
    SensorGroup* mRideGroup;
    SensorGroup* mEyeGroup;
    SensorGroup* mSimpleGroup;
    SensorGroup* mMapObjGroup;
    SensorGroup* mCharacterGroup;
};

// A sensor as the pair test reads it: its place and radius beside it, so the pairs walk one array instead of each
// sensor and its actor once per partner.
struct SensorHitEntry {
    f32 mX, mY, mZ, mRadius;
    const LiveActor* mHost;
    HitSensor* mSensor;
};

class SensorGroup {
public:
    explicit SensorGroup(int maxSensors);

    void add(HitSensor*);
    void remove(HitSensor*);
    void clear() const;
    void gatherActive();

    s32 mMaxSensors;
    s32 mSensorCount;
    HitSensor** mSensors;
    // This frame's active sensors (valid, host not clipped), in mSensors' order; mMaxSensors entries, made and grown
    // with mSensors in their arena. Null in the module with hook points, which tests the pairs as the game does.
    s32 mActiveCount;
    SensorHitEntry* mActive;
};

namespace MR {
    void initHitSensorGroup(HitSensor*);
};  // namespace MR
