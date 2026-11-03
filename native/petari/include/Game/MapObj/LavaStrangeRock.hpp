#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;

class LavaStrangeRock : public LiveActor {
public:
    enum Type {
        Type_0,
        Type_1,
        Type_2,
        Type_3,
    };

    /// @brief Creates a new `LavaStrangeRock`.
    /// @param pName A pointer to the null-terminated name of the object.
    LavaStrangeRock(const char* pName);

    virtual ~LavaStrangeRock();
    virtual void init(const JMapInfoIter&);
    virtual void control();

    void initModelAndClipping(const JMapInfoIter&);
    void initEffect();
    void exeWait();

    s32 mRockType;
    s32 _90;
    LodCtrl* mLodCtrlPlanet;
};
