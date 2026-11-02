#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/NativeMesh.hpp"

class OceanRingPipe;
class JUTTexture;

class OceanRingPipeInside : public LiveActor {
public:
    OceanRingPipeInside(const OceanRingPipe*);

    virtual ~OceanRingPipeInside();
    virtual void init(const JMapInfoIter&);
    virtual void movement();
    virtual void draw() const;

    void initMesh();
    void loadMaterial() const;
    void sendGD();

    const OceanRingPipe* mRingPipe;
    f32 mTexU0;
    f32 mTexV0;
    f32 mTexU1;
    f32 mTexV1;
    JUTTexture* mWaterPipeInsideTex;
    // The pipe's surface as the renderer draws it.
    NativeMesh mMesh;
};
