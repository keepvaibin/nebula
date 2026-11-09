#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailPosture : public MapPartsFunction {
public:
    // Rail arg 0.
    enum {
        POSTURE_NONE = 0,
        POSTURE_RAIL_DIR = 1,
        POSTURE_RAIL_DIR_FRONT = 2,  // Moves as POSTURE_RAIL_DIR.
        POSTURE_RAIL_DIR_SHADOW_GRAVITY = 3,
    };

    MapPartsRailPosture(LiveActor*);

    virtual void init(const TableIter&) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;

    bool isPostureTypeRailDirRailUseShadowGravity() const;

    void exeDoNothing();
    void exeMove();

    Pos3f mPostureMtx;
    s32 mMovePosture;
};
