#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include <nebula_geometry.hpp>

class CollectCounter : public LayoutActor {
public:
    CollectCounter(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void exeHide();
    void exeShow();

    void setCount(s32 num);
    void setPosition();

    Vec2f mFollowPosition;
};
