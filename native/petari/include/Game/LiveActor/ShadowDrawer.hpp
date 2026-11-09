#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ShadowController;

class ShadowDrawer : public NameObj {
public:
    ShadowDrawer(const char*);

    virtual ~ShadowDrawer() override {
    }

    ShadowController* getController() const;
    void setShadowController(ShadowController*);

    // Where the shape stands (a jump of this between two steps starts a new object) and the count of the host's lives
    // (a new life starts one). Both default to the shadow's controller: its drop position and its host's epoch.
    virtual void calcDrawAnchor(Vec3f* pOut) const;
    virtual u32 calcDrawLives() const;
    // The draws after this belong to an object named by the drawer's slot, the host's life and the run of steps the
    // anchor has moved smoothly, so the host renewed or the shadow jumped never blends from where it was.
    void beginDrawObject() const;

    ShadowController* mController;
    mutable Vec3f mDrawAnchor;
    mutable u32 mAnchorJumps;
    mutable bool mHasAnchor;
};