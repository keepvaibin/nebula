#pragma once

#include "Game/Camera/CameraTower.hpp"

class CameraTripodBoss : public CameraTower {
public:
    CameraTripodBoss(const char* pName = "\216\117\213\162\203\173\203\130\203\112\203\201\203\211" /* 三脚ボスカメラ */);
    virtual ~CameraTripodBoss();

    virtual CamTranslatorBase* createTranslator();

    void arrangeRound();

    /* 0x8C */ f32 mAngleY;
};
