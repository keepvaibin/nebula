#pragma once

#include "Game/Util/Array.hpp"

#include "Game/MapObj/PowerStar.hpp"

struct PowerStarRequestInfo {
    PowerStar* mStar;
    int mStarNum;
    bool mIsAppear;
};

class PowerStarHolder : public NameObj {
public:
    PowerStarHolder(const char*);

    virtual ~PowerStarHolder();
    virtual void init(const JMapInfoIter&);

    void registerPowerStar(PowerStar*, int);
    void requestAppearPowerStar(int, const TVec3f*, bool);
    void appearPowerStarWithoutDemo(int);
    PowerStar* getAppearedPowerStar(int) const;
    PowerStarRequestInfo* findPowerStarRequestInfo(int) const;

    MR::GrowTable< PowerStarRequestInfo*, 0x10 > mInfos;
    s32 mNumInfos;
};

namespace MR {
    void registerPowerStar(PowerStar*, int);
};  // namespace MR

class PowerStarFunction {
public:
    static bool isEndPowerStarAppearDemo(int);
    static PowerStar* findPowerStar(int);
};
