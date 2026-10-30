#pragma once

#include "Game/Scene/Scene.hpp"

class LayoutActor;
class LogoFader;
class SimpleLayout;

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

    bool isDisplayStrapRemineder() const;


    void exeStrapFadein();
    void exeStrapDisplay();
    void exeStrapFadeout();
    void exeMountGameData();
#ifdef TARGET_PC
    void exeWaitRayTracing();
#endif
    void exeWaitReadDoneSystemArchive();
    void exeDeactive();
    void initLayout();
    bool tryFadeinLayout(LayoutActor* pActor);
    bool tryFadeoutLayout(LayoutActor* pActor);
    bool tryFadeinLayout();
    bool tryFadeoutLayout();

private:
    /* 0x14 */ SimpleLayout* mStrapLayout;
    /* 0x18 */ LogoFader* mLogoFader;
};
