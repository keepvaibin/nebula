#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapParts : public LiveActor {
public:
    MapParts(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual s32 getSensorNumMax() const;
    virtual void connectToScene();

    static s32 getMoveStartSignalTime();
    void initModelAndCollision(const TableIter&);

    Vec3f mClipCenter;
};
