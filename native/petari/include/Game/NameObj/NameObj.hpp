#pragma once

#include "Game/Util/Functor.hpp"
#include "Game/Util/Table.hpp"

class NameObjHolder;

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
    // Its class for mods (lumacore::classOf): bit 15 set when it is the object's own class, else a base's.
    u16 mLumaClass;
    // The index into the NameObjExecuteInfo array (which grows past any 16-bit count).
    s32 mExecutorIdx;
    // Names its draws from frame to frame.
    mutable u32 mDrawSlot;
    // Counts the object's lives (MotionSignal).
    u32 mMotionEpoch;
    // Its place in the scene's movement, calc-anim and draw category lists (NameObjCategoryList::mPlaceSlot); -1: none.
    s32 mListPlace[3];
    // An actor's place in its draw buffer executer's list (DrawBufferExecuter); -1: none.
    s32 mDrawBufferPlace;
    // The holder that finds it by name, and its place there (NameObjHolder::add); null and -1: none.
    NameObjHolder* mNameHolder;
    s32 mHolderPlace;
};

class NameObjFunction {
public:
    static void requestMovementOn(NameObj*);
    static void requestMovementOff(NameObj*);
};
