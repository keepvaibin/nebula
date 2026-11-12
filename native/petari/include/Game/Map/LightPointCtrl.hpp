#pragma once

#include "Game/Util/Color.hpp"
#include "Game/Render/FrameList.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;

struct PointLightInfo {
    Vec mPos;
    NebulaColor8 mColor;
    f32 mRefDistance;
    f32 mRefBrightness;
    LightFalloff mDistAttnFn;
};

class LightPointCtrl {
public:
    LightPointCtrl();

    void loadPointLight();
    void update();
    void requestPointLight(const LiveActor* pActor, Vec3f pos, Color8 color, f32 intensity, s32 duration);
    // Nebula: a request by any owner (`pKey`: an actor, or a mod's light of draw.light), `distance` from it to the
    // player now; the nearest asking wins, as among actors.
    void requestPointLightBy(const void* pKey, f32 distance, Vec3f pos, Color8 color, f32 intensity, s32 duration);
    void clearPointLight(PointLightInfo* pInfo);
    void blendPointLight(PointLightInfo* pDst, const PointLightInfo& rStart, const PointLightInfo& rEnd, f32 t);

    bool tryBlendStart();

    void updatePointLight();

    s32 getStep() const {
        return mStep;
    }

    s32 mStep;
    s32 mBlendTime;
    // Who asked: an actor, or a mod's light (compared only, never read through).
    const void* mCurrentActor;
    const void* mPreviousActor;
    const void* mCandidateActor;
    f32 mCandidateDistance;  // the candidate's distance to the player when it asked
    PointLightInfo* mCurrentInfo;
    PointLightInfo* mTargetInfo;
    PointLightInfo* mPreviousInfo;
};
