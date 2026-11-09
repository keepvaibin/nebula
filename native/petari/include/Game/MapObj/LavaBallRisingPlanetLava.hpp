#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class LavaBallRisingPlanetLava : public MapObjActor {
public:
    LavaBallRisingPlanetLava(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeScaleUp();
    void exeScaleDown();

private:
    void updateScale(f32 startScale, f32 endScale, const Nerve* pNextNerve);
};
