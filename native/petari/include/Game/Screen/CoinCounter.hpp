#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class CoinCounter : public LayoutActor {
public:
    /// @brief Creates a new `CoinCounter`.
    /// @param pName The pointer to the null-terminated name of the object.
    CoinCounter(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

    void forceAppear();
    void disappear();
    bool isWait() const;
    void updateCounter();
    void exeHide();
    void exeAppear();
    void exeWait();
    void exeDisappear();

private:
    s32 mCoinNum;
    s32 mCoinDisplayNum;
    s32 mInvalidCountUpFrame;
    CounterLayoutAppearer* mLayoutAppearer;
    CountUpPaneRumbler* mPaneRumbler;
    bool mIsForceAppear;
    TVec2f mFollowPos;
};
