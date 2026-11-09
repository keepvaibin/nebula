#pragma once

#include "Game/Screen/LayoutActor.hpp"

class OneUpBoard : public LayoutActor {
public:
    OneUpBoard();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void popup(s32 oneUpNum);
    void calcPos();
    void exeWait();
    void exeAppear();
};
