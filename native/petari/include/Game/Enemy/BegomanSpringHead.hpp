#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class BegomanHead : public PartsModel {
public:
    BegomanHead(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx, int drawBufferType, bool useHostLight)
        : PartsModel(pHost, pName, pModelName, pMtx, drawBufferType, useHostLight), mHostScale() {
    }

    virtual void calcAndSetBaseMtx() override;

    Vec3f* mHostScale;
};

class BegomanSpringHead : public BegomanHead {
public:
    BegomanSpringHead(LiveActor* pActor, MtxPtr pMtx);

    virtual void init(const TableIter& rIter) override;

    bool isSpringHop();
    s32 getHopEndBckFrameMax();
    void tryHopStart();
    void tryHopEnd();
    void tryHopJump();
    void forceWaitImmediately();
    void exeWait();
    void exeHopStart();
    void exeHopWait();
    void exeHopEnd();
    void exeHopJump();
};
