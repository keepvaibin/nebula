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

    void initDisplayList();
    void loadMaterial() const;
    void sendGD();

    /* 0x8C */ const OceanRingPipe* mRingPipe;
    /* 0x90 */ f32 mTexU;
    /* 0x94 */ JUTTexture* mWaterPipeIndirectTex;
    /* 0x98 */ JUTTexture* mWaterPipeHighLightTex;
    // The pipe's surface as the renderer draws it.
    NativeMesh mMesh;
};