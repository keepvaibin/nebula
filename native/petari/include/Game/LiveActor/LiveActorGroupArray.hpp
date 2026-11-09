#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/Array.hpp"

class HitSensor;
class PlacementId;

class MsgSharedGroup : public LiveActorGroup {
public:
    MsgSharedGroup(const char*, s32, const TableIter&);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void sendMsgToGroupMember(u32, HitSensor*, const char*);

    PlacementId* mIdInfo;
    u32 mMsg;
    HitSensor* mSensor;
    const char* mSensorName;
};

class LiveActorGroupArray : public NameObj {
public:
    LiveActorGroupArray(const char* pName);

    virtual void init(const TableIter&);

    LiveActorGroup* getLiveActorGroup(const LiveActor*) const;
    LiveActorGroup* createGroup(const TableIter&, const char*, s32);
    LiveActorGroup* findGroup(const LiveActor*) const;
    LiveActorGroup* findGroup(const TableIter&) const;
    LiveActorGroup* entry(LiveActor*, const TableIter&, const char*, s32);

    MR::InlineVector< MsgSharedGroup*, 32 > mGroups;
};
