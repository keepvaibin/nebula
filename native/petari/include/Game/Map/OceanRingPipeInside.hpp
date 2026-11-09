#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"

class OceanRingPipe;
class GameTexture;

class OceanRingPipeInside : public LiveActor {
public:
    OceanRingPipeInside(const OceanRingPipe*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual void draw() const override;

    void initMesh();
    SiteMaterial loadMaterial() const;
    void sendGD();

    const OceanRingPipe* mRingPipe;
    f32 mTexU0;
    f32 mTexV0;
    f32 mTexU1;
    f32 mTexV1;
    GameTexture* mWaterPipeInsideTex;
    // The pipe's surface as the renderer draws it.
    SiteMesh< VertexPT2 > mMesh;
};
