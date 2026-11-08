#pragma once

#include "Game/NPC/NPCActor.hpp"

class FollowCollisionArea;

class SignBoard : public NPCActor {
public:
    SignBoard(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();

};
