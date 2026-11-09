#pragma once

#include "Game/NameObj/NameObj.hpp"

class AudCameraWatcher : public NameObj {
public:
    AudCameraWatcher();

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    void playAtmosphereSE();

};
