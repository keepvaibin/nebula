#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StrayTico;

class CollectTico : public LiveActor {
public:
    CollectTico(const char*);

    virtual void init(const JMapInfoIter&);

    void exeWait();
    void exeCompleteDemo();
    void exeFlash();
    void exeAppearPowerStar();
    inline void exeTryStartDemo();
    s32 calcNoRescuedCount() const;
    void startAppearPowerStar();

    StrayTico** mStrayTicos;
    s32 mTicoNum;
    TVec3f _94;
    u8 _A0;
};
