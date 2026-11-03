#pragma once

#include "Game/NPC/TicoComet.hpp"

class BenefitItemLifeUp;
class BenefitItemOneUp;

class TicoShop : public TicoComet {
public:
    /// @brief Creates a new `TicoShop`.
    /// @param pName A pointer to the null-terminated name of the object.
    TicoShop(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void startReactionSound();

    bool branchFunc(u32);
    bool eventFunc(u32);

    void exeDemo();

    BenefitItemOneUp* mOneUp;
    BenefitItemLifeUp* mLifeUp;
    bool mChoseOneUp;
    bool mChoseLifeUp;
};
