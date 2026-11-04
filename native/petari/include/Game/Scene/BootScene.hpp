#pragma once

#include "Game/Scene/Scene.hpp"

class BootScene : public Scene {
public:
    /// @brief Creates a new `BootScene`.
    BootScene();

    /// @brief Destroys the `BootScene`.
    virtual ~BootScene();

    virtual void init();
    virtual void update();
    virtual void calcAnim();
    virtual void draw() const;

    bool isBeforeTitle() const;

    void exeMountGameData();
    void exeWaitRayTracing();
    void exeWaitReadDoneSystemArchive();
    void exeDeactive();

};
