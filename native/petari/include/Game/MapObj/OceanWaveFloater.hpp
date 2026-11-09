#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class FloaterFloatingForceTypeSpring;
class WaveFloatingForce;

class OceanWaveFloater : public MapObjActor {
public:
    OceanWaveFloater(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void initAfterPlacement() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    f32 getCurrentSinkDepth() const;
    void controlEffect();

    FloaterFloatingForceTypeSpring* mSpring;
    WaveFloatingForce* mForce;
    Vec3f mRipplePos;
    s32 mSoundDelay;
    Vec3f mMapGroundUpper;
    f32 mOffset;
    bool mCanRipple;
};
