#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include <nebula_geometry.hpp>

class CollectCounter : public LayoutActor {
public:
    CollectCounter(const char*);

    virtual ~CollectCounter();
    virtual void init(const JMapInfoIter&);
    virtual void control();

    void exeHide();
    void exeShow();

    void setCount(s32);
    bool tryEndShow();
    void setPosition();

    Vec2f mFollowPosition;
};
