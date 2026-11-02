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
    void calcPosAndAxisY(TVec3f*, TVec3f*, f32) const;

    void calcNearestPos(TVec3f*, const TVec3f&) const;
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
