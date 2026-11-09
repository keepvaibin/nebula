#pragma once

#include "Game/Util/NameIndex.hpp"

class CodeTable;
class TableIter;
class Triangle;

enum CollisionFloorCode {
    CollisionFloorCode_Normal,
    CollisionFloorCode_Death,
    CollisionFloorCode_Slip,
    CollisionFloorCode_NoSlip,
    CollisionFloorCode_DamageNormal,
    CollisionFloorCode_Ice,
    CollisionFloorCode_JumpLow,
    CollisionFloorCode_JumpMiddle,
    CollisionFloorCode_JumpHigh,
    CollisionFloorCode_Slider,
    CollisionFloorCode_DamageFire,
    CollisionFloorCode_JumpNormal,
    CollisionFloorCode_FireDance,
    CollisionFloorCode_Sand,
    CollisionFloorCode_Glass,
    CollisionFloorCode_DamageElectric,
    CollisionFloorCode_PullBack,
    CollisionFloorCode_Sink,
    CollisionFloorCode_SinkPoison,
    CollisionFloorCode_Slide,
    CollisionFloorCode_WaterBottomH,
    CollisionFloorCode_WaterBottomM,
    CollisionFloorCode_WaterBottomL,
    CollisionFloorCode_Wet,
    CollisionFloorCode_Needle,
    CollisionFloorCode_SinkDeath,
    CollisionFloorCode_Snow,
    CollisionFloorCode_RailMove,
    CollisionFloorCode_AreaMove,
    CollisionFloorCode_Press,
    CollisionFloorCode_NoStampSand,
    CollisionFloorCode_SinkDeathMud,
    CollisionFloorCode_Brake,
    CollisionFloorCode_GlassIce,
    CollisionFloorCode_JumpParasol,

    CollisionFloorCode_End,
};

enum CollisionWallCode {
    CollisionWallCode_Normal,
    CollisionWallCode_NotWallJump,
    CollisionWallCode_NotWallSlip,
    CollisionWallCode_NotGrab,
    CollisionWallCode_GhostThroughCode,
    CollisionWallCode_NotSideStep,
    CollisionWallCode_Rebound,
    CollisionWallCode_Fur,
    CollisionWallCode_NoAction,

    CollisionWallCode_End,
};

enum CollisionSoundCode {
    CollisionSoundCode_Null,
    CollisionSoundCode_Soil,
    CollisionSoundCode_Lawn,
    CollisionSoundCode_Stone,
    CollisionSoundCode_Marble,
    CollisionSoundCode_WoodThick,
    CollisionSoundCode_WoodThin,
    CollisionSoundCode_Metal,
    CollisionSoundCode_Snow,
    CollisionSoundCode_Ice,
    CollisionSoundCode_Shallow,
    CollisionSoundCode_Sand,
    CollisionSoundCode_Beach,
    CollisionSoundCode_Carpet,
    CollisionSoundCode_Mud,
    CollisionSoundCode_Honey,

    CollisionSoundCode_End,
};

enum CollisionCameraCode {
    CollisionCameraCode_NoThrough,
    CollisionCameraCode_Through,

    CollisionCameraCode_End,
};

class CollisionCode {
public:
    CollisionCode();

    u32 getCameraID(const Triangle& rTriangle);
    u32 getCode(const TableIter& rIter, CodeTable* pCodeTable, const char* pName);
    void createFloorTable();
    void createWallTable();
    void createSoundTable();
    void createCameraTable();
    u32 getFloorCode(const TableIter& rIter);
    u32 getWallCode(const TableIter& rIter);
    u32 getSoundCode(const TableIter& rIter);
    u32 getCameraCode(const TableIter& rIter);
    u32 getCameraID(const TableIter& rIter);

    CodeTable* mFloorTable;
    CodeTable* mWallTable;
    CodeTable* mSoundTable;
    CodeTable* mCameraTable;
};

class CodeTable {
public:
    CodeTable(u32 numMax);

    void add(const char* pName, u32 code);

    MR::NameIndex mIndex;  // a code's name to the code
};
