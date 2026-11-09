#pragma once

#include "Game/Util/Functor.hpp"
#include "Game/Util/Table.hpp"

// The base of every scene object.
class NameObj {
public:
    // mMovementFlag bits. Requests take effect at the next syncWithFlags.
    enum MovementFlag {
        MovementFlag_Off = 1,
        MovementFlag_SuspendRequest = 2,
        MovementFlag_ResumeRequest = 4,
    };

    NameObj(const char* pName);

    virtual ~NameObj();
    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void calcViewAndEntry();

    void initWithoutIter();
    void setName(const char* pName);

    const char* getName() const {
        return mName;
    }

    void executeMovement();
    void requestSuspend();
    void requestResume();
    void syncWithFlags();

    // Loading-only continuation. Actors may yield before the scene is made playable.
    virtual bool initStep(const TableIter& iter, uint64_t) { init(iter); return true; }

    const char* mName;
    // MovementFlag bits.
    u16 mMovementFlag;
    // The index into the NameObjExecuteInfo array.
    s32 mExecutorIdx;
    // Names its draws from frame to frame.
    mutable u32 mDrawSlot;
    // Counts the object's lives (MotionSignal).
    u32 mMotionEpoch;
};

class NameObjFunction {
public:
    static void requestMovementOn(NameObj*);
    static void requestMovementOff(NameObj*);
};
