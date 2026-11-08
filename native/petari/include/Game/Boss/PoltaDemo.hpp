#pragma once

#include "Game/Boss/PoltaActionBase.hpp"

class Polta;

class PoltaDemo : public PoltaActionBase {
public:
    PoltaDemo(Polta* pPolta);

    virtual void init();

    void startDemoSetting();
    void startOpeningDemo();
    void startPowerUpDemo();
    void startDownDemo();
    void startTryDemo(const char* pDemoName, const Nerve* pDemoNerve);

    void exeTryStartDemo();
    void exeOpeningDemo();
    void exePowerUpDemo();
    void exeDownDemo();

    const Nerve* mCurDemoNerve;
    const char* mCurDemoName;
};
