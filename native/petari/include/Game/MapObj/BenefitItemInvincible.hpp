#pragma once

#include "Game/MapObj/BenefitItemObj.hpp"

class BenefitItemInvincible : public BenefitItemObj {
public:
    BenefitItemInvincible(const char*);

    virtual void init(const TableIter&) override;
    virtual void appearThrowUp() override;
    virtual void initModelAndEfx() override;
    virtual void exeCatch() override;
    virtual void runBck(const char*) override;
    virtual void runEfx(const char*) override;
    virtual void stopEfx(const char*) override;
    virtual void initEscape() override;
    virtual void doEscape() override;

    Vec3f mUpAxis;
    Vec3f mFwdAxis;
};
