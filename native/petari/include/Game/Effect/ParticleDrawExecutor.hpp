#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class EffectSystem;
class NameObjAdaptor;

class ParticleDrawExecutor {
public:
    ParticleDrawExecutor(const EffectSystem*, bool);

    static void initDraw();
    void draw3D() const;
    void draw2D() const;
    void drawIndirect() const;
    void drawAfterIndirect() const;
    void drawFor2DModel() const;
    void drawForBloomEffect() const;
    void drawAfterImageEffect() const;
    void drawWithViewMtx3D(const TPos3f&) const;
    void drawWithViewMtxAfterIndirect(const TPos3f&) const;
    void drawWithViewMtxForBloomEffect(const TPos3f&) const;
    void drawWithViewMtxAfterImageEffect(const TPos3f&) const;
    void initDrawAdaptor();

    const EffectSystem* mHost;
    NameObjAdaptor* _4;
    NameObjAdaptor* _8;
    NameObjAdaptor* _C;
    NameObjAdaptor* _10;
    NameObjAdaptor* _14;
    NameObjAdaptor* _18;
    NameObjAdaptor* _1C;
    bool _20;
    bool _21;
};
