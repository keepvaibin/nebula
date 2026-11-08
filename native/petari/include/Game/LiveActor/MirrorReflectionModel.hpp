#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MirrorReflectionModel : public LiveActor {
public:
    // The host's model seen in the mirror; it follows pHostMtx, or the host's SRT when null.
    MirrorReflectionModel(const LiveActor* pHost, const char* pName, const char* pInternalName, MtxPtr pHostMtx);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;
    virtual void calcAndSetBaseMtx() override;

    const LiveActor* mHost;
    MtxPtr mHostMtx;
    bool mIsHostGone;  // dead, clipped or hidden: this is off the draw lists
};
