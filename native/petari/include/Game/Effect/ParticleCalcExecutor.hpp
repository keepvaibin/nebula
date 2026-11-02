#pragma once

class EffectSystem;
class NameObjAdaptor;

class ParticleCalcExecutor {
public:
    ParticleCalcExecutor(const EffectSystem* pEffectSystem, bool createAdaptors);
    void movementNormal();
    void movementIgnorePause3D();
    void movementIgnorePause2D();
    void movementCheckUpdate();
    void requestMovementOnPauseIgnore();
    void initMovementAdaptor();

    const EffectSystem* mEffectSystem;
    NameObjAdaptor* mNormalAdaptor;
    NameObjAdaptor* mIgnorePause3DAdaptor;
    NameObjAdaptor* mIgnorePause2DAdaptor;
    NameObjAdaptor* mCheckUpdateAdaptor;
    bool mMovementEnabled;
    bool mUpdateRequested;
};
