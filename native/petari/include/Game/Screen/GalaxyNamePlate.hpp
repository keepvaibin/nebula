#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct GalaxyNamePlatePrioritied;

class GalaxyNamePlate : public LayoutActor {
public:
    GalaxyNamePlate(const char*, bool);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();

    void showUnknown(bool);
    void showNew(bool);
    void showOpen(bool);
    void show(const char16_t*, bool);
    void setPos3D(const TVec3f&);
    void setShowBalloonNozzle(bool);
    void show(const char16_t*, s32, bool, bool);

    void exeAppearReady();
    void exeAppear();
    void exeWait();

    const char* mGalaxyName;
    bool _24;
    bool _25;
    bool mShowBalloonNozzle;
    GalaxyNamePlatePrioritied* mDrawerEntry;
    s32 _2C;
    bool _30;
};
