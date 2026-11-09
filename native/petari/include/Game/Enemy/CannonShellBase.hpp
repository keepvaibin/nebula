#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class CannonShellBase : public LiveActor {
public:
    CannonShellBase(const char* pName) : LiveActor(pName) {
    }

    virtual ~CannonShellBase() override {
    }

    virtual void launch(const Vec3f& rStartPos, const Vec3f& rVelocity) = 0;

    virtual f32 getBaseScale() const {
        return 1.0f;
    }
};

// A fixed pool of shells; getValidShell hands out a dead one.
class CannonShellHolder {
public:
    CannonShellHolder(int num);

    void registerCannonShell(CannonShellBase*);
    CannonShellBase* getValidShell() const;
    void killActiveShells() const;

    MR::Vector< MR::AssignableArray< CannonShellBase* > > mShells;
};
