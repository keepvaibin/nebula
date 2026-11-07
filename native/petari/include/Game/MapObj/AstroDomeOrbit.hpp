#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class Color8;

class AstroDomeOrbit : public LiveActor {
public:
    AstroDomeOrbit();

    virtual ~AstroDomeOrbit();
    virtual void init(const TableIter&);
    virtual void draw() const;

    void drawBloom() const;
    void setup(s32);
    void moveCoord();
    void calcGalaxyPos(Vec3f*) const;
    SiteMaterial loadMaterial(const Color8&) const;
    void drawCelling(TypedDraw&, f32, bool, f32) const;
    void drawSide(TypedDraw&, f32, bool, f32) const;
    f32 calcRepeatedRotateCoord(f32) const;

    f32 mOrbitRadius;
    f32 mAngle;
};
