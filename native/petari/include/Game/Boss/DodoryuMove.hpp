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

        virtual void execute() override;

        f32 mMaxSpeed;
        f32 mSpeed;
    };

    class MoveStateChase : public MoveState {
    public:
        MoveStateChase(MoveStateHolder* pHolder) : MoveState(pHolder), mAccel(), mLateralDamping(), mMaxSpeed(), mTurnAngle() {
        }

        virtual void execute() override;

        f32 mAccel;
        f32 mLateralDamping;
        f32 mMaxSpeed;
        f32 mTurnAngle;
    };

    class MoveStateDetour : public MoveState {
    public:
        MoveStateDetour(MoveStateHolder* pHolder);

        virtual void execute() override;

        bool isPlayerOppositeFromWall() const;

        f32 mAccel;
        f32 mLateralDamping;
        f32 mMaxSpeed;
        f32 mTurnAngle;
    };

    class MoveStateEscape : public MoveState {
    public:
        MoveStateEscape(MoveStateHolder* pHolder);

        virtual void execute() override;

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

        virtual void execute() override;

        f32 mTurnAngle;
        f32 mSpeed;
    };

    class MoveStateNull : public MoveState {
    public:
        MoveStateNull(MoveStateHolder* pHolder);

        virtual void execute() override;
    };

    class MoveStateHolder {
    public:
        MoveStateHolder(Dodoryu* pHost);

        void execute();
        void shiftRail(f32 speed);
        void shiftChase(f32 accel, f32 lateralDamping, f32 maxSpeed, f32 turnAngle);
        void shiftEscape(f32 speed, s32 dirStepMin, s32 dirStepMax, f32 dirAngleMax, f32 turnAngle);
        void shiftJumpOut(f32 speed, f32 turnAngle);
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
