#pragma once

#include <nebula_geometry.hpp>

class Dodoryu;

namespace DodoryuSub {
    class MoveStateHolder;

    class MoveState {
    public:
        MoveState(MoveStateHolder* pHolder);

        virtual void execute() = 0;

        MoveStateHolder* mHolder;
        Dodoryu* mHost;
        s32 mStep;
    };

    class MoveStateRail : public MoveState {
    public:
        MoveStateRail(MoveStateHolder* pHolder);

        virtual void execute();

        f32 mMaxSpeed;
        f32 mSpeed;
    };

    class MoveStateChase : public MoveState {
    public:
        MoveStateChase(MoveStateHolder* pHolder) : MoveState(pHolder), mAccel(), mLateralDamping(), mMaxSpeed(), mTurnAngle() {
        }

        virtual void execute();

        f32 mAccel;
        f32 mLateralDamping;
        f32 mMaxSpeed;
        f32 mTurnAngle;
    };

    class MoveStateDetour : public MoveState {
    public:
        MoveStateDetour(MoveStateHolder* pHolder);

        virtual void execute();

        bool isPlayerOppositeFromWall() const;

        f32 mAccel;
        f32 mLateralDamping;
        f32 mMaxSpeed;
        f32 mTurnAngle;
    };

    class MoveStateEscape : public MoveState {
    public:
        MoveStateEscape(MoveStateHolder* pHolder);

        virtual void execute();

        s32 mDirStepLeft;
        s32 mDirStepMin;
        s32 mDirStepMax;
        f32 mDirAngleMax;
        f32 mTurnAngle;
        f32 mMaxSpeed;
        f32 mSpeed;
        Vec3f mDir;
    };

    class MoveStateJumpOut : public MoveState {
    public:
        MoveStateJumpOut(MoveStateHolder* pHolder);

        virtual void execute();

        f32 mTurnAngle;
        f32 mSpeed;
    };

    class MoveStateNull : public MoveState {
    public:
        MoveStateNull(MoveStateHolder* pHolder);

        virtual void execute();
    };

    class MoveStateHolder {
    public:
        MoveStateHolder(Dodoryu* pHost);

        void execute();
        void shiftRail(f32);
        void shiftChase(f32, f32, f32, f32);
        void shiftEscape(f32, s32, s32, f32, f32);
        void shiftJumpOut(f32, f32);
        void shiftNull();
        void brake();
        void setRailSpeed(f32);

        Dodoryu* mHost;
        MoveState* mNextState;
        MoveState* mCurrentState;
        MoveStateRail* mRailState;
        MoveStateChase* mChaseState;
        MoveStateDetour* mDetourState;
        MoveStateEscape* mEscapeState;
        MoveStateJumpOut* mJumpOutState;
        MoveStateNull* mNullState;
    };
};  // namespace DodoryuSub
