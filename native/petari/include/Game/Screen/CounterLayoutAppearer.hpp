#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class LayoutActor;

class CounterLayoutAppearer : public NerveExecutor {
public:
    CounterLayoutAppearer(LayoutActor*, const TVec2f&);

    void update();
    void appear(const TVec2f&);
    void disappear();
    bool isAppeared() const;
    bool isDisappeared() const;
    void reset();
    void updateLayoutOffset(f32);

    void setAppearOffset(const TVec2f& rOffset) {
        mAppearOffset.x = rOffset.x;
        mAppearOffset.y = rOffset.y;
    }

    void exeHide();
    void exeAppear();
    void exeShow();
    void exeDisappear();

private:
    LayoutActor* mLayout;
    TVec2f mAppearOffset;
    TVec2f mFollowPos;
    TVec2f _1C;
};
