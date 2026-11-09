#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

// ElectricRail's arg 3, ElectricRailMoving's arg 4 + 2.
enum ElectricRailType { ElectricRail_Yellow, ElectricRail_Red, ElectricRail_YellowMoving, ElectricRail_RedMoving };

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

    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void draw() const override;
    virtual void control() override;

    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*) const;
    void registerRail(const LiveActor*, ElectricRailType);
    ModelObj* createModel(ElectricRailType) const;

    MR::InlineArray< ModelObj*, 4 > mRailModels;
    MR::InlineVector< RailItem*, 64 > mRailItems;
    bool mIsActive;
};

namespace ElectricRailFunction {
    void registerRail(const LiveActor*, ElectricRailType);
    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*);
    f32 getHitSensorRadius();
};  // namespace ElectricRailFunction
