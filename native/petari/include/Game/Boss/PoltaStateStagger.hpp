#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class Polta;
class HitSensor;

class PoltaStateStagger : public ActorStateBase< Polta > {
public:
    PoltaStateStagger(Polta*);

    virtual void appear();

    void exeWait();

    bool isEnableSensor() const;
    void setActionName(const char*);

    const char* mActionName;
    bool mIsClockwise;
};
