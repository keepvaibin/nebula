#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LiveActorGroup;
class MiniatureGalaxy;

class MiniatureGalaxyHolder : public LiveActor {
public:
    MiniatureGalaxyHolder();

    virtual void init(const TableIter&) override;

    void registerActor(LiveActor*, const TableIter&);
    bool isRegisteredActor(const LiveActor*);
    MiniatureGalaxy* findMiniatureGalaxy(const char*) const;
    void killAllMiniatureGalaxy();
    s32 calcIndex(const LiveActor*) const;
    void updateCometStatus();

    LiveActorGroup* mMiniatureGalaxyGroup;
    bool mIsRegistered;
    MiniatureGalaxy* mCometGalaxy;
    s32 mCometID;
};

class MiniatureGalaxyFunction {
public:
    static void registerMiniatureGalaxyToHolder(LiveActor*, const TableIter&);
    static s32 getMiniatureGalaxyNum();
    static s32 calcMiniatureGalaxyIndex(const LiveActor*);
    static void updateCometStatus();
    static MiniatureGalaxy* getCometLandMiniatureGalaxy();
    static s32 getCometNameId();
    static MiniatureGalaxy* getPointingMiniatureGalaxy();
};
