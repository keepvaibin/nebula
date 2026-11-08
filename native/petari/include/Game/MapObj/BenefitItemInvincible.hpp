#pragma once

#include "Game/MapObj/BenefitItemObj.hpp"

class BenefitItemInvincible : public BenefitItemObj {
public:
    BenefitItemInvincible(const char*);

    virtual void init(const TableIter&);
    virtual void appearThrowUp();
    virtual void initModelAndEfx();
    virtual void exeCatch();
    virtual void runBck(const char*);
    virtual void runEfx(const char*);
    virtual void stopEfx(const char*);
    virtual void initEscape();
    virtual void doEscape();

    Vec3f mUpAxis;
    Vec3f mFwdAxis;
};
