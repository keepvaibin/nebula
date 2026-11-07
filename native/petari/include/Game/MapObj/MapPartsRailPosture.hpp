#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailPosture : public MapPartsFunction {
public:
    MapPartsRailPosture(LiveActor*);

    virtual ~MapPartsRailPosture();
    virtual void init(const TableIter&);
    virtual bool isWorking() const;
    virtual void start();
    virtual void end();

    bool isPostureTypeRailDirRailUseShadowGravity() const;

    void exeDoNothing();
    void exeMove();

    Pos3f _18;
    s32 mMovePosture;
};
