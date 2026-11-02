#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/PlayerUtil.hpp"

class JUTTexture;

class WaterPlant : public LiveActor {
public:
    class PlantData {
    public:
        /// @brief Creates a new `PlantData`.
        PlantData();

        TVec3f mPosition;
        TVec3f mRotation;
        TVec3f mSpinSwingDir;
        TVec3f mTempSpinSwingDir;
        f32 mSpinSwingStrength;
        s32 mSpinWaveSwingTimer;
        s32 mSpinWaveTimer;
        TVec3f mSpinWaveSwingVec1;
        TVec3f mSpinWaveSwingVec2;
        TVec3f mSpinWaveSwingVec3;
        f32 mHeight1;
        f32 mHeight2;
        f32 mHeight3;
        u8 pad_6C;
        f32* mSwingStrength1;
        f32* mSwingStrength2;
        f32* mSwingStrength3;

        bool newSpinWaveTimerIsGreater(s32 newTimer) {
            return !(newTimer < mSpinWaveTimer);
        }
    };

    /// @brief Creates a new `WaterPlant`.
    /// @param pName A pointer to the null-terminated name of the object.
    WaterPlant(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;

    void initPlant();
    void initPlantAfterPlacement();
    void drawDirect() const;

    PlantData* getData(int i) {
        return &mPlantDataArray[i];
    }

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
    /// @brief Creates a new `WaterPlantDrawInit`.
    WaterPlantDrawInit();

    virtual void movement();

    virtual void initDraw() const;

    void loadTex(int);
    void updateSwingPos();

    f32 mAngleOffset;
    f32* mSwingPosTable;
    JUTTexture* mPlantATex;
    JUTTexture* mPlantBTex;
    JUTTexture* mPlantCTex;
    JUTTexture* mPlantDTex;
};
