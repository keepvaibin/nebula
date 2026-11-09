#pragma once

#include "Game/Scene/Scene.hpp"

class BootScene : public Scene {
public:
    BootScene();

    virtual void init() override;
    virtual void update() override;
    virtual void calcAnim() override;
    virtual void draw() const override;

    bool isBeforeTitle() const;

    void exeMountGameData();
    void exeWaitPreparation();
    void exeWaitReadDoneSystemArchive();
    void exeDeactive();

};
