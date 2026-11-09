#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class QuestionBoxGalleryObj : public MapObjActor {
public:
    QuestionBoxGalleryObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;

    void startSwitchOn();
    void startSwitchOff();

    void exeOnWait();
    void exeOnStart();
    void exeOffWait();
    void exeOffStart();
};
