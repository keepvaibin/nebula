#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

enum ElectricRailType { ElectricRail_Red, ElectricRail_Yellow, ElectricRail_YellowMoving, ElectricRail_RedMoving };

class MaterialDraw;
class MaterialEntry;
class ModelObj;

// A rail the holder draws, through its model's material.
class ElectricRailDrawn : public LiveActor {
public:
    ElectricRailDrawn(const char* pName) : LiveActor(pName) {
    }
    virtual void drawWith(MaterialDraw& rMaterial) const = 0;
};

struct RailItem {
    ElectricRailType mRailType;
    const ElectricRailDrawn* mHost;
    MaterialEntry* mMatPacket;
};

class ElectricRailHolder : public LiveActor {
public:
    ElectricRailHolder(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void calcAnim();
    virtual void draw() const;
    virtual void control();

    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*) const;
    void registerRail(const LiveActor*, ElectricRailType);
    ModelObj* createModel(ElectricRailType) const;

    MR::FixedArray< ModelObj*, 4 > mRailModels;
    MR::Vector< MR::FixedArray< RailItem*, 64 > > mRailItems;
    bool mIsActive;
};

namespace ElectricRailFunction {
    void registerRail(const LiveActor*, ElectricRailType);
    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*);
    f32 getHitSensorRadius();
    u32 calcMeshSize(u32, s32);
};  // namespace ElectricRailFunction
