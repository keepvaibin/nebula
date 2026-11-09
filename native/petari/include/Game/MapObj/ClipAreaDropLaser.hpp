#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>
#include <cstddef>

class ClipAreaDropLaser : public LiveActor {
public:
    ClipAreaDropLaser(const char* pName);
    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void draw() const override;
    void exeWait();
    void exeMove();
    void incrementDrawCount();

    static const int cPointNum = 0x40;  // the trail, one point each update (oldest overwritten)
    Vec3f mPoints[cPointNum];
    f32 mNumPointsToDraw;
    int mPointIndexToSkipDraw;
    int mDrawCount;
    f32 mSpeed;
};
