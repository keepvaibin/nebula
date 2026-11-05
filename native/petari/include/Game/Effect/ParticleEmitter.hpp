#pragma once

#include <nebula_geometry.hpp>

class JPABaseEmitter;

class ParticleEmitter {
public:
    ParticleEmitter();

    void invalidate();
    void init(u16);
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

    JPABaseEmitter* mEmitter;
    bool mPaused;
    bool mStopped;  // Not sure if it is stopped, but good chance it is.
};
