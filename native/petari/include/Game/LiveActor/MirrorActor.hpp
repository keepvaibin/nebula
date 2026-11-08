#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MirrorActor : public LiveActor {
public:
    MirrorActor(LiveActor*, const char*, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;

    bool isHostInTheOtherSideOfMirror() const;
    Vec3f getHostCenterPos() const;

    LiveActor* mHost;
    Vec3f mCenter;
    f32 mRadius;
    bool mIsDisconnected;
};
