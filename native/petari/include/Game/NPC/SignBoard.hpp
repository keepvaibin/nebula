#pragma once

#include "Game/NPC/NPCActor.hpp"

class FollowCollisionArea;

class SignBoard : public NPCActor {
public:
    SignBoard(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWait();

};
