#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class PlantPoint;
class PlantRailInfo;
class GameTexture;

class PlantStalk : public LiveActor {
public:
    PlantStalk();

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void drawGrowUp(TypedDraw& rDraw) const;

    bool updateGrowUp();
    void calcPosAndAxisY(Vec3f*, Vec3f*, f32) const;

    void calcNearestPos(Vec3f*, const Vec3f&) const;
    f32 calcPlayerCoord() const;

    s32 mNumPlantPoints;
    PlantPoint** mPlantPoints;
    PlantRailInfo* mRailInfo;
    f32 mStalkLength;
    f32 mGrowthPercent;
    f32 mGrowSpeed;
    s32 mGrowAccelTime;
    s32 mGrownPlantPoints;
};

class PlantStalkDrawInit : public NameObj {
public:
    PlantStalkDrawInit(const char*);

    virtual SiteMaterial makeMaterial() const;
    // What its objects draw with.
    SiteMaterial mMaterial;

    GameTexture* mTexture;
};
