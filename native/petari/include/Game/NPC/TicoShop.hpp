#pragma once

#include "Game/NPC/TicoComet.hpp"

class BenefitItemLifeUp;
class BenefitItemOneUp;

class TicoShop : public TicoComet {
public:
    TicoShop(const char* pName);

    virtual void init(const TableIter&);
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
