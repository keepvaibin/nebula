#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class PlayerSeArea : public AreaObj {
public:
    PlayerSeArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    virtual const char* getManagerName() const override {
        return "PlayerSeArea";
    }

    s32 mSeType;
};
