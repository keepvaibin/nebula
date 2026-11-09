#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"

class OceanRingPipe;
class GameTexture;

class OceanRingPipeOutside : public LiveActor {
public:
    OceanRingPipeOutside(const OceanRingPipe*);

    virtual void init(const TableIter&);
    virtual void movement();
    virtual void draw() const;

    void initMesh();
    SiteMaterial loadMaterial() const;
    void sendGD();

    const OceanRingPipe* mRingPipe;
    f32 mTexU;
    GameTexture* mWaterPipeIndirectTex;
    GameTexture* mWaterPipeHighLightTex;
    // The pipe's surface as the renderer draws it.
    SiteMesh< VertexPNT > mMesh;
};