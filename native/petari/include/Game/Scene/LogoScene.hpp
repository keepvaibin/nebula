#pragma once

#include "Game/Scene/Scene.hpp"

class LogoFader;

class LogoScene : public Scene {
public:
    /// @brief Creates a new `LogoScene`.
    LogoScene();

    /// @brief Destroys the `LogoScene`.
    virtual ~LogoScene();

    virtual void init();
    virtual void update();
    virtual void calcAnim();
    virtual void draw() const;

    bool isBeforeTitle() const;

    void exeMountGameData();
    void exeWaitRayTracing();
    void exeWaitReadDoneSystemArchive();
    void exeDeactive();
    void initLayout();

private:
    LogoFader* mLogoFader;
};
