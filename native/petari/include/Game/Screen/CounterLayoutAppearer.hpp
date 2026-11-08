#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class LayoutActor;

class CounterLayoutAppearer : public NerveExecutor {
public:
    CounterLayoutAppearer(LayoutActor*, const Vec2f&);

    void update();
    void appear(const Vec2f&);
    void disappear();
    bool isAppeared() const;
    bool isDisappeared() const;
    void reset();
    void updateLayoutOffset(f32);

    void setAppearOffset(const Vec2f& rOffset) {
        mAppearOffset.x = rOffset.x;
        mAppearOffset.y = rOffset.y;
    }

    void exeHide();
    void exeAppear();
    void exeShow();
    void exeDisappear();

private:
    LayoutActor* mLayout;
    Vec2f mAppearOffset;
    Vec2f mFollowPos;
    Vec2f mExtraOffset;
};
