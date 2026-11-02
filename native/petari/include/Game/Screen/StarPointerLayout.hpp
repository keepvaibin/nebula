#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActor;
class StarPointerCommandStream;
class StarPointerDirector;
class StarPointerBlur;
class StarPointerNumber;

// TODO: what are the missing values?
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
    StarPointerLayout(const char* pName = "\203\130\203\136\201\133\203\174\203\103\203\223\203\136\203\214\203\103\203\101\203\105\203\147" /* スターポインタレイアウト */);

    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void initWithPort(s32);
    bool startCommandStream(const LiveActor*, const TVec3f*, bool);
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
    void endNotConnected();
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
    void calcPoseNozzle(const TVec2f&, const TVec2f&);
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

    TVec2f mPosition;
    StarPointerKind mPointerKind;
    AnimType mAnimType;
    s32 mTouchTimer;
    s32 mPadChannel;
    f32 mRadius;
    f32 _3C;
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
    u32 _74;
};

class StarPointerNumber : public LayoutActor {
public:
    StarPointerNumber(StarPointerLayout*, s32, const char*);

    virtual void control();

    bool isOffState() const;
    void startAnimPosition(u32);

    StarPointerLayout* mParent;
    s32 mPadChannel;
    AnimType mAnimType;
    bool mIsAppear;
    s32 mAppearTime;
};
