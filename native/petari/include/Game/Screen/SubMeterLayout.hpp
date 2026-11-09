#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SubMeterLayout : public LayoutActor {
public:
    SubMeterLayout(const char*, const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void setLifeRatio(f32);
    void requestDeactivate();
    void requestAppear();
    void requestFrameIn();
    void requestFrameOut();

    u8 mPhase;
    u16 mLifeFrame;
    u16 mFullLifeFrame;
};
