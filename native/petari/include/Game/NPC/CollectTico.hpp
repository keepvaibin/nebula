#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StrayTico;

class CollectTico : public LiveActor {
public:
    CollectTico(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeCompleteDemo();
    void exeFlash();
    void exeAppearPowerStar();
    inline void exeTryStartDemo();
    s32 calcNoRescuedCount() const;
    void startAppearPowerStar();

    StrayTico** mStrayTicos;
    s32 mTicoNum;
    Vec3f mStarPos;
    u8 mIsDemoCast;
};
