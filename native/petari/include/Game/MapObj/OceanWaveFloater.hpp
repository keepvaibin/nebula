#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class FloaterFloatingForceTypeSpring;
class WaveFloatingForce;

class OceanWaveFloater : public MapObjActor {
public:
    OceanWaveFloater(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    f32 getCurrentSinkDepth() const;
    void controlEffect();

    FloaterFloatingForceTypeSpring* mSpring;
    WaveFloatingForce* mForce;
    TVec3f mRipplePos;
    s32 mSoundDelay;
    TVec3f mMapGroundUpper;
    f32 mOffset;
    bool mCanRipple;
};
