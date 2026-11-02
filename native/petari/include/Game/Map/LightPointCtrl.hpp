#pragma once

#include "Game/Util/Color.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;

struct PointLightInfo {
    Vec mPos;
    NebulaColor8 mColor;
    f32 mRefDistance;
    f32 mRefBrightness;
    J3DLightFalloff mDistAttnFn;
};

class LightPointCtrl {
public:
    LightPointCtrl();

    void loadPointLight();
    void update();
    void requestPointLight(const LiveActor* pActor, TVec3f pos, Color8 color, f32 intensity, s32 duration);
    void clearPointLight(PointLightInfo* pInfo);
    void blendPointLight(PointLightInfo* pDst, const PointLightInfo& rStart, const PointLightInfo& rEnd, f32 t);

    bool tryBlendStart();
    bool isUpdateCandidateActor(const LiveActor* pActor) const;

    void updatePointLight();

    s32 getStep() const {
        return mStep;
    }

    s32 mStep;
    s32 mBlendTime;
    const LiveActor* mCurrentActor;
    const LiveActor* mPreviousActor;
    const LiveActor* mCandidateActor;
    PointLightInfo* mCurrentInfo;
    PointLightInfo* mTargetInfo;
    PointLightInfo* mPreviousInfo;
};
