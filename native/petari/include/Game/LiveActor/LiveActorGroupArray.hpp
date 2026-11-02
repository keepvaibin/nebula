#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/Array.hpp"

class HitSensor;
class JMapIdInfo;

class MsgSharedGroup : public LiveActorGroup {
public:
    MsgSharedGroup(const char*, s32, const JMapInfoIter&);

    virtual void init(const JMapInfoIter&) override;
    virtual void movement() override;

    void sendMsgToGroupMember(u32, HitSensor*, const char*);

    JMapIdInfo* mIdInfo;
    u32 mMsg;
    HitSensor* mSensor;
    const char* mSensorName;
    char mGroupName[48];
};

class LiveActorGroupArray : public NameObj {
public:
    LiveActorGroupArray(const char* pName);

    virtual void init(const JMapInfoIter&);

    LiveActorGroup* getLiveActorGroup(const LiveActor*) const;
    LiveActorGroup* createGroup(const JMapInfoIter&, const char*, s32);
    LiveActorGroup* findGroup(const LiveActor*) const;
    LiveActorGroup* findGroup(const JMapInfoIter&) const;
    LiveActorGroup* entry(LiveActor*, const JMapInfoIter&, const char*, s32);

    MR::Vector< MR::FixedArray< MsgSharedGroup*, 32 > > mGroups;
};
