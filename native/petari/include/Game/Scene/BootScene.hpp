#pragma once

#include "Game/Scene/Scene.hpp"

class BootScene : public Scene {
public:
    BootScene();

    virtual void init();
    virtual void update();
    virtual void calcAnim();
    virtual void draw() const;

    bool isBeforeTitle() const;

    void exeMountGameData();
    void exeWaitPreparation();
    void exeWaitReadDoneSystemArchive();
    void exeDeactive();

};
