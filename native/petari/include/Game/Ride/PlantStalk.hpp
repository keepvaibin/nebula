#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PlantPoint;
class PlantRailInfo;
class JUTTexture;

class PlantStalk : public LiveActor {
public:
    PlantStalk();

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void drawGrowUp() const;

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

    virtual void initDraw() const;

    JUTTexture* mTexture;
};
