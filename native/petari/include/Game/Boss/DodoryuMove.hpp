#pragma once

#include <JSystem/JGeometry/TVec.hpp>

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

        f32 _10;
        f32 _14;
    };

    class MoveStateChase : public MoveState {
    public:
        MoveStateChase(MoveStateHolder* pHolder) : MoveState(pHolder), _10(), _14(), _18(), _1C() {
        }

        virtual void execute();

        f32 _10;
        f32 _14;
        f32 _18;
        f32 _1C;
    };

    class MoveStateDetour : public MoveState {
    public:
        MoveStateDetour(MoveStateHolder* pHolder);

        virtual void execute();

        bool isPlayerOppositeFromWall() const;

        f32 _10;
        f32 _14;
        f32 _18;
        f32 _1C;
    };

    class MoveStateEscape : public MoveState {
    public:
        MoveStateEscape(MoveStateHolder* pHolder);

        virtual void execute();

        s32 _10;
        s32 _14;
        s32 _18;
        f32 _1C;
        f32 _20;
        f32 _24;
        f32 _28;
        TVec3f _2C;
    };

    class MoveStateJumpOut : public MoveState {
    public:
        MoveStateJumpOut(MoveStateHolder* pHolder);

        virtual void execute();

        f32 _10;
        f32 _14;
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
