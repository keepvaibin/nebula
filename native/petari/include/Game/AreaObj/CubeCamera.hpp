#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class CubeCameraArea : public AreaObj {
public:
    enum ECategory {
        ECategory_Normal,
        ECategory_Swim,
        ECategory_WaterSurface,
        ECategory_GCapture,
        ECategory_FooFighter,
        ECategory_None,
    };

    CubeCameraArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual bool isInVolume(const Vec3f&) const override;
    virtual s32 getCategoryArg() const;

    bool isValidCameraID() const;
    u16 getCameraID() const;

    static void setCurrentCategory(s32);

    static s32 sCubeCategory;

    u32 getZoneID() const {
        return mZoneID;
    }

    s32 mCategoryMask;
    u32 mZoneID;
};

class CubeCameraMgr : public AreaObjMgr {
public:
    CubeCameraMgr(s32, const char*);

    void initAfterLoad();
    void sort();
};
