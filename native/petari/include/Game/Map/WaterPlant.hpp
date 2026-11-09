#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/PlayerUtil.hpp"

class GameTexture;

class WaterPlant : public LiveActor {
public:
    class PlantData {
    public:
        Vec3f mPosition;
        Vec3f mRotation;
        Vec3f mSpinSwingDir;
        Vec3f mTempSpinSwingDir;
        f32 mSpinSwingStrength;
        s32 mSpinWaveSwingTimer;
        s32 mSpinWaveTimer;
        Vec3f mSpinWaveSwingVec1;
        Vec3f mSpinWaveSwingVec2;
        Vec3f mSpinWaveSwingVec3;
        f32 mHeight1;
        f32 mHeight2;
        f32 mHeight3;
        // Entries of WaterPlantDrawInit::mSwingPosTable, top to bottom.
        f32* mSwingStrength1;
        f32* mSwingStrength2;
        f32* mSwingStrength3;
    };

    WaterPlant(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;

    void initPlant();
    void initPlantAfterPlacement();
    void drawDirect(TypedDraw& rDraw, const Vec3f& rSide) const;

    s32 mPlantNum;
    PlantData* mPlantDataArray;
    f32 mAreaRadius;
    bool mIsSpin;
    f32 mHalfWidth;
    f32 mHeight;
    s32 mType;
};

class WaterPlantDrawInit : public NameObj {
public:
    WaterPlantDrawInit();

    virtual void movement();


    SiteMaterial material(int) const;
    void updateSwingPos();

    f32 mAngleOffset;
    f32* mSwingPosTable;
    // The camera's X axis in world space, this frame.
    Vec3f mCameraSide;
    GameTexture* mPlantATex;
    GameTexture* mPlantBTex;
    GameTexture* mPlantCTex;
    GameTexture* mPlantDTex;
};
