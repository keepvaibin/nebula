#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

struct ResTIMG;
class GameTexture;
class FootPrintInfo;

class FootPrintInfo {
public:
    FootPrintInfo();

    Vec3f mPos;
    Vec3f mUp;
    Vec3f mNormal;

    /// @brief Frames left before becoming invalid. Also controls the footprint's alpha.
    s32 mTimeLeft;

    /// @brief Invalid prints will no longer be updated nor drawn.
    bool mIsValid;

    bool mIsFlipX;
};

class FootPrint : public NameObj {
public:
    FootPrint(const char*, s32);
    FootPrint(const char*, s32, s32);

    virtual void movement();
    virtual void draw() const;

    void setTexture(ResTIMG*);
    bool addPrint(const Vec3f& rPos, const Vec3f& rUp, const Vec3f& rNormal, bool isFlipX);
    void clear();
    void initMember(s32, s32);
    void clearForce();
    Vec3f* getPrintPos(u32) const;
    void invalidate(u32);
    bool isValid(u32) const;

    GameTexture* mTexture;

    /// @brief Array of footprints.
    FootPrintInfo* mPrints;

    Vec3f mLastPrintPos;

    /// @brief Size of the mPrints array.
    s32 mPrintMaxNum;

    /// @brief Amount of active footprints (counting invalid ones). Caps at mPrintMaxNum.
    s32 mPrintNum;

    /// @brief The index used when adding a new footprint. Will wrap around and overwrite the oldest footprints.
    s32 mRotatingPrintIndex;

    f32 mSideOffset;
    f32 mHeight;
    f32 mWidth;

    /// @brief The minimum distance for a new print to be registered.
    f32 mMinPrintDistance;

    bool mIsFading;
};
