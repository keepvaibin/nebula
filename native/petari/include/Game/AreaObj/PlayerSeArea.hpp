#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class PlayerSeArea : public AreaObj {
public:
    PlayerSeArea(int, const char*);

    virtual ~PlayerSeArea();
    virtual void init(const JMapInfoIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "PlayerSeArea";
    }

    s32 _3C;
};
