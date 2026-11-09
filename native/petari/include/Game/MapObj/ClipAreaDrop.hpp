#pragma once

#include "Game/MapObj/ClipArea.hpp"

class ClipAreaShapeSphere;

class ClipAreaDrop : public ClipArea {
public:
    ClipAreaDrop(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void setBaseSize(f32);

    void exeWait();

    ClipAreaShapeSphere* mShapeSphere;
    f32 mBaseSize;
};
