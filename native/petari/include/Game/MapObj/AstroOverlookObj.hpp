#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AstroOverlookObj : public LiveActor {
public:
    AstroOverlookObj(const char*);

    virtual void control();

    u8 mIsLayoutHidden;
};