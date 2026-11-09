#pragma once

#include <nebula_geometry.hpp>

class EmitterCore;
class ParticleEmitterHolder;

class ParticleEmitter {
public:
    ParticleEmitter();

    void invalidate();
    void init();
    void pauseOn();
    void pauseOff();
    bool isValid() const;
    bool isContinuousParticle() const;

    // In MultiEmitterAccess
    void setGlobalRotation(const Vec3s&);
    void setGlobalScale(const Vec3f&);
    void setGlobalSRTMatrix(const Mtx);
    void setGlobalPrmColor(u8, u8, u8);
    void setGlobalEnvColor(u8, u8, u8);

    EmitterCore* mEmitter;
    bool mPaused;
    bool mUpdated;  // set by its first holder update, which starts a linked emitter's callback
    ParticleEmitterHolder* mHolder;  // the pool this record lies in, whichever effect system deletes it
    u32 mPlace;                      // its place in that pool, counted across chunks
};
