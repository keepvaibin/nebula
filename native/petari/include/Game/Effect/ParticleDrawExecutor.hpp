#pragma once

#include <nebula_geometry.hpp>

class EffectSystem;

// Draws an effect system's particles in each of the scene's particle passes.
class ParticleDrawExecutor {
public:
    // `connect`: each pass gets its own draw object in the scene (the scene's effect system, not the scenario select's).
    ParticleDrawExecutor(const EffectSystem*, bool connect);

    void draw3D() const;
    void draw2D() const;
    void drawIndirect() const;
    void drawAfterIndirect() const;
    void drawFor2DModel() const;
    void drawForBloomEffect() const;
    void drawAfterImageEffect() const;
    void drawWithViewMtx3D(const Pos3f&) const;
    void drawWithViewMtxAfterIndirect(const Pos3f&) const;
    void drawWithViewMtxForBloomEffect(const Pos3f&) const;
    void drawWithViewMtxAfterImageEffect(const Pos3f&) const;

    const EffectSystem* mHost;

private:
    void connectToScene();
};
