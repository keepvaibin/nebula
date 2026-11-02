#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Coin;
class Spine;

class TripodBossCoin : public NameObj {
public:
    /// @brief Creates a new `TripodBossCoin`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossCoin(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void movement();

    void exeNonActive();
    void exeActive();
    void exeEnd();

    Coin* mCoin;
    Spine* mSpine;
    TPos3f _14;
    s32 mJointID;
};
