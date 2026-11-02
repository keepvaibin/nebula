#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class StarPieceCounter : public LayoutActor {
public:
    /// @brief Creates a new `StarPieceCounter`.
    /// @param pName The pointer to the null-terminated name of the object.
    StarPieceCounter(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

    void forceAppear(bool);
    void disappear(bool);
    bool isWait() const;
    void forceSync();
    bool tryOnModeTicoEat(bool);
    bool tryOffModeTicoEat();
    void updateCounter();
    void updateCounterValue();
    bool isValidAppearSituation() const;
    bool isDispCenter() const;
    bool tryChangeModeTicoEat(int);
    void exeHide();
    void exeAppear();
    void exeWait();
    void exeDisappear();

    s32 mStarPieceNum;
    s32 mStarPieceDisplayNum;
    s32 mInvalidCountUpFrame;
    CounterLayoutAppearer* mLayoutAppearer;
    CountUpPaneRumbler* mPaneRumbler;
    TVec2f mFollowPos;
    s32 _3C;
    int mMode;
};
