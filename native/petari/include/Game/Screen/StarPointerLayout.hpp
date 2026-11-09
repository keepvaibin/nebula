#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include <nebula_geometry.hpp>

namespace Ui {
    class Pane;
}  // namespace Ui

class LiveActor;
class StarPointerCommandStream;
class StarPointerDirector;
class StarPointerBlur;
class StarPointerNumber;

// The pointer pictures the layout switches on; kinds 2 and 5 have no name of their own in its branches.
enum StarPointerKind {
    StarPointerKind_NULL = -1,
    StarPointerKind_StarPointer = 0,
    StarPointerKind_HandPointer = 1,
    StarPointerKind_2 = 2,
    StarPointerKind_HandPointerReactionWithCrossCursor = 3,
    StarPointerKind_FingerPointer = 4,
    StarPointerKind_5 = 5,
    StarPointerKind_StarPointerNozzle = 6,
};

enum AnimType {
    AnimType_NULL = -1,
    AnimType_Finger = 0,
    AnimType_HandGuu = 1,
    AnimType_HandPaa = 2,
    AnimType_StarPointer = 3,
};

class StarPointerLayout : public LayoutActor {
public:
    StarPointerLayout(const char* pName = "スターポインタレイアウト");

    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void initWithPort(s32);
    bool startCommandStream(const LiveActor*, const Vec3f*, bool);
    void endCommandStream(const LiveActor*);
    void forceEndCommandStream();
    bool isCommandStream(const LiveActor*);

    void exeWait();
    void exeOnReaction();
    void endOnReaction();
    void exeGrip();
    void exeHold();
    void endHold();
    void exeRelease();
    void exeOutScreen();
    void exeNotConnected();
    void exeHide();
    void endHide();

    void tearDownHold();
    void updateTouch();
    bool isTouch();
    void hideAll();
    void tearDownHide();
    void drawDecoration() const;
    void setPosition();
    void rotateTest();
    void calcPoseNozzle(const Vec2f&, const Vec2f&);
    void changeLayout(StarPointerKind);
    bool isChanceToRumble() const;
    void hideBlur();
    void resetSameNerve();
    void changeToStarPointer();
    void changeToHandPointer();
    void startAnimStarPointer(const char*);
    void startAnimHandPaa();
    void startAnimHandGuu();
    void startAnimHandFinger();
    void updateDecoration();

    void show() {
        if (!mIsAppear) {
            mAppearTime = -3;
        }
        mIsAppear = true;
    }

    void hide() {
        mIsAppear = false;
    }

    void validate() {
        mIsPointerValid = true;
    }

    void invalidate() {
        mIsPointerValid = false;
    }

    bool isAppear() const {
        return mIsAppear && mAppearTime >= 0;
    }

    AnimType getAnimType() const {
        return mAnimType;
    }

    bool isSingleTouch() {
        return mSingleTouch || mStartSingleTouch;
    }

    f32 getRadius() const {
        return mRadius;
    }

    Vec2f mPosition;
    StarPointerKind mPointerKind;
    AnimType mAnimType;
    s32 mTouchTimer;
    s32 mPadChannel;
    f32 mRadius;
    StarPointerDirector* mDirector;
    StarPointerBlur* mBlur;
    StarPointerCommandStream* mCommandStream;
    StarPointerNumber* mNumber;
    const LiveActor* mActor;
    u64
        mNewTouchedID;  // NOTE: The use of this variable is a bit strange. It holds a unique identifier to check if the last touched object has
                        // changed (usually via pointer), but this identifier can be pretty much anything: actor pointer, layout pointer + pane string
                        // pointer, pointer to raw position data, etc. Basically: a pointer comparison with extra steps.
    u64 mTouchedID;
    bool mIsNewTouch;
    bool mStartTouch;
    bool mStartDisableShoot;
    bool mSingleTouch;
    bool mStartSingleTouch;
    bool mShootDisabled;
    bool mIsPointerValid;
    bool mIsAppear;
    s32 mAppearTime;
    // The panes shown, hidden and tested every frame, bound in initWithPort.
    Ui::Pane* mStarPointerPane = nullptr;
    Ui::Pane* mHandPointerPane = nullptr;
    Ui::Pane* mHandArrowPane = nullptr;
    Ui::Pane* mPicNozzlePane = nullptr;
};

class StarPointerNumber : public LayoutActor {
public:
    StarPointerNumber(StarPointerLayout*, s32, const char*);

    virtual void control() override;

    bool isOffState() const;
    void startAnimPosition(u32);

    StarPointerLayout* mParent;
    s32 mPadChannel;
    AnimType mAnimType;
    bool mIsAppear;
    s32 mAppearTime;
};
