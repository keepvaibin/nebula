#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

struct ResTIMG;
class JUTTexture;
class FootPrintInfo;

class FootPrintInfo {
public:
    FootPrintInfo();

    TVec3f mPos;
    TVec3f _C;
    TVec3f _18;

    /// @brief Frames left before becoming invalid. Also controls the footprint's alpha.
    s32 mTimeLeft;

    /// @brief Invalid prints will no longer be updated nor drawn.
    bool mIsValid;

    bool _29;
};

class FootPrint : public NameObj {
public:
    FootPrint(const char*, s32);
    FootPrint(const char*, s32, s32);

    virtual void movement();
    virtual void draw() const;

    void setTexture(ResTIMG*);
    bool addPrint(const TVec3f&, const TVec3f&, const TVec3f&, bool);
    void clear();
    void initMember(s32, s32);
    void clearForce();
    TVec3f* getPrintPos(u32) const;
    void invalidate(u32);
    bool isValid(u32) const;

    JUTTexture* _C;

    /// @brief Array of footprints.
    FootPrintInfo* mPrints;

    TVec3f mLastPrintPos;

    /// @brief Size of the mPrints array.
    s32 mPrintMaxNum;

    /// @brief Amount of active footprints (counting invalid ones). Caps at mPrintMaxNum.
    s32 mPrintNum;

    /// @brief The index used when adding a new footprint. Will wrap around and overwrite the oldest footprints.
    s32 mRotatingPrintIndex;

    f32 _2C;
    f32 _30;
    f32 _34;

    /// @brief The minimum distance for a new print to be registered.
    f32 mMinPrintDistance;

    bool _3C;
};
