#pragma once

#include "Game/NPC/TicoComet.hpp"

class BenefitItemLifeUp;
class BenefitItemOneUp;

class TicoShop : public TicoComet {
public:
    TicoShop(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void startReactionSound() override;

    bool branchFunc(u32);
    bool eventFunc(u32);

    void exeDemo();

    BenefitItemOneUp* mOneUp;
    BenefitItemLifeUp* mLifeUp;
    bool mChoseOneUp;
    bool mChoseLifeUp;
};
