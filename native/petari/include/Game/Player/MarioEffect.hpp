#pragma once

#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class MarioActor;
class EmitterCore;

struct MovingFollowMtx {
    MovingFollowMtx() {
        mLocalMtx.identity();
        mWorldMtx.identity();
        mBaseMtx = nullptr;
        mEmitter = nullptr;
        mFrame = 0;
    }

    u32 mFrame;  // frames followed (at 2 the emitter goes on alone)
    Mtx34f mLocalMtx;  // the emitter in the base's space
    Mtx34f mWorldMtx;  // the emitter in the world
    MtxPtr mBaseMtx;  // the base it follows
    EmitterCore* mEmitter;  // the emitter
};

class MarioEffect : public MarioModule, public MultiEmitterCallBackBase {
public:
    MarioEffect(MarioActor*);

    virtual void execute(EmitterCore*);

    virtual ~MarioEffect();

    EmitterCore* addRequest(const char*, MtxPtr);
    // The entry still follows its emitter: not back in the pool (forceDeleteEmitter's 0x200) nor made again for another owner.
    bool isFollowing(const MovingFollowMtx&) const;
    void updateFollowMtx(MovingFollowMtx*);
    void playSwingEffect();
    void doCubeEffect();

    s32 mCubeArg0;  // the effect cube's arg 0 (1: the swing effect takes mCubeArg1)
    s32 mCubeArg1;  // the effect cube's arg 1: the swing effect's kind (0: the floor material's)
    u8 mCubeColorOn;  // the cube's colors tint the common effects
    Color8 mCubePrmColor;  // the cube's prm color
    Color8 mCubeEnvColor;  // the cube's env color
    Mtx34f mCommonEffectMtx;  // the common effect's base (playCommonEffect)
    MR::GrowTable< MovingFollowMtx, 16 > mFollows;
    s32 mFollowCount;
};
