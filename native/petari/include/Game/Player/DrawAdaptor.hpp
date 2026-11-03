#pragma once

#include "Game/NameObj/NameObj.hpp"

class DrawAdaptor : public NameObj {
public:
    DrawAdaptor(const MR::FunctorBase& rFunc, int drawType);

    virtual void draw() const;

private:
    const MR::FunctorBase* mFunc;
};
