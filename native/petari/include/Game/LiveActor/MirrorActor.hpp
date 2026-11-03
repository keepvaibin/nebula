#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MirrorActor : public LiveActor {
public:
    MirrorActor(LiveActor*, const char*, const char*);

    virtual ~MirrorActor();

    virtual void init(const JMapInfoIter&) override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;

    bool isHostInTheOtherSideOfMirror() const;
    TVec3f getHostCenterPos() const;

    LiveActor* mHost;
    TVec3f mCenter;
    f32 mRadius;
    bool mIsDisconnected;
};
