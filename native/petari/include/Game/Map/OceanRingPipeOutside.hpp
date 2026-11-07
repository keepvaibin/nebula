#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"

class OceanRingPipe;
class JUTTexture;

class OceanRingPipeOutside : public LiveActor {
public:
    OceanRingPipeOutside(const OceanRingPipe*);

    virtual ~OceanRingPipeOutside();
    virtual void init(const TableIter&);
    virtual void movement();
    virtual void draw() const;

    void initMesh();
    SiteMaterial loadMaterial() const;
    void sendGD();

    const OceanRingPipe* mRingPipe;
    f32 mTexU;
    JUTTexture* mWaterPipeIndirectTex;
    JUTTexture* mWaterPipeHighLightTex;
    // The pipe's surface as the renderer draws it.
    SiteMesh< VertexPNT > mMesh;
};