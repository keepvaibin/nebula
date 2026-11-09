#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class Color8;

// A miniature galaxy's orbit ring in the observatory, drawn as a flat band in the normal and bloom passes.
class AstroDomeOrbit : public LiveActor {
public:
    AstroDomeOrbit();

    virtual void init(const TableIter&) override;
    virtual void draw() const override;

    void drawBloom() const;
    void setup(s32 galaxyIndex);
    void moveCoord();
    void calcGalaxyPos(Vec3f*) const;

    // The ring divides into 64 segments; the last point repeats the first.
    static const s32 cRingPointNum = 65;

private:
    void drawBand(const Color8& rColor, f32 width, f32 height) const;
    void drawFlat(TypedDraw&, f32 width, bool isTop, f32 height) const;
    void drawSide(TypedDraw&, f32 width, bool isOuter, f32 height) const;

public:
    f32 mOrbitRadius;
    f32 mAngle;
    f32 mRingCos[cRingPointNum];
    f32 mRingSin[cRingPointNum];
};
