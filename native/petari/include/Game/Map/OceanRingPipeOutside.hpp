#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/NativeMesh.hpp"

class OceanRingPipe;
class JUTTexture;

class OceanRingPipeOutside : public LiveActor {
public:
    OceanRingPipeOutside(const OceanRingPipe*);

    virtual ~OceanRingPipeOutside();
    virtual void init(const JMapInfoIter&);
    virtual void movement();
    virtual void draw() const;

    void initMesh();
    void loadMaterial() const;
    void sendGD();

    const OceanRingPipe* mRingPipe;
    f32 mTexU;
    JUTTexture* mWaterPipeIndirectTex;
    JUTTexture* mWaterPipeHighLightTex;
    // The pipe's surface as the renderer draws it.
    NativeMesh mMesh;
};