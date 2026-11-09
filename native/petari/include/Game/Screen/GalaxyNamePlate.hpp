#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct GalaxyNamePlatePrioritied;

class GalaxyNamePlate : public LayoutActor {
public:
    GalaxyNamePlate(const char*, bool);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;

    void showUnknown(bool);
    void showNew(bool);
    void showOpen(bool);
    void show(const char16_t*, bool);
    void setPos3D(const Vec3f&);
    void setShowBalloonNozzle(bool);
    void show(const char16_t*, s32, bool, bool);

    void exeAppearReady();
    void exeAppear();
    void exeWait();

    const char* mGalaxyName;
    bool mIsShownThisFrame;  // show() ran since the last control()
    bool mIsAppearNow;  // last show() skipped the ready wait
    bool mShowBalloonNozzle;
    GalaxyNamePlatePrioritied* mDrawerEntry;
    s32 mStateFrame;  // frame of the Unknown anim: the galaxy state (1 is new)
    bool mPlayNewSe;  // the new-galaxy sound has not played yet
};
