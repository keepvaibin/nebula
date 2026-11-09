#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    void createActorCameraName(MR::FormattedName& rName, const LiveActor* pActor, const ActorCameraInfo* pInfo) {
        if (pInfo->mCameraSetID & 0x8000) {
            rName.format("%s共通%03d", pActor->getName(), pInfo->mCameraSetID - 0x8000);
        } else {
            rName.format("%s固有%03d", pActor->getName(), pInfo->mCameraSetID);
        }
    }

    void createMultiActorCameraName(MR::FormattedName& rName, const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        if (pInfo->mCameraSetID & 0x8000) {
            rName.format("%s共通%s%03d", pActor->getName(), pEventName, pInfo->mCameraSetID - 0x8000);
        } else {
            rName.format("%s固有%s%03d", pActor->getName(), pEventName, pInfo->mCameraSetID);
        }
    }
};  // namespace

namespace MR {
    void initAnimCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        MR::FormattedName animName("%s.canm", pEventName);

        declareEventCameraAnim(pInfo, actorEventName.c_str(), getResourceHolder(pActor)->mFileInfoTable->getRes(animName.c_str()));
    }

    ActorCameraInfo* createActorCameraInfo(const TableIter& rIter) {
        return new ActorCameraInfo(rIter);
    }

    bool createActorCameraInfoIfExist(const TableIter& rIter, ActorCameraInfo** ppInfo) {
        ActorCameraInfo newInfo(rIter);

        if (newInfo.mCameraSetID == -1) {
            return false;
        }

        (*ppInfo) = createActorCameraInfo(rIter);

        return true;
    }

    bool initActorCamera(const LiveActor* pActor, const TableIter& rIter, ActorCameraInfo** ppInfo) {
        return initMultiActorCamera(pActor, rIter, ppInfo, nullptr);
    }

    bool initMultiActorCamera(const LiveActor* pActor, const TableIter& rIter, ActorCameraInfo** ppInfo, const char* pEventName) {
        if (*ppInfo == nullptr) {
            *ppInfo = createActorCameraInfo(rIter);
        }

        return initMultiActorCameraNoInit(pActor, (*ppInfo), pEventName);
    }

    bool initMultiActorCameraNoInit(const LiveActor* pActor, ActorCameraInfo* pInfo, const char* pEventName) {
        if (pInfo->mCameraSetID < 0) {
            return false;
        }

        MR::FormattedName name;

        if (pEventName != nullptr) {
            ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
        } else {
            ::createActorCameraName(name, pActor, pInfo);
        }

        declareEventCamera(pInfo, name.c_str());

        return true;
    }

    void initActorCameraProgrammable(const LiveActor* pActor) {
        declareEventCameraProgrammable(pActor->getName());
    }

    bool startActorCameraNoTarget(const LiveActor* pActor, const ActorCameraInfo* pInfo, s32 frame) {
        CameraTargetArg targetArg = CameraTargetArg();

        return startMultiActorCameraTargetOther(pActor, pInfo, nullptr, targetArg, frame);
    }

    bool startActorCameraTargetPlayer(const LiveActor* pActor, const ActorCameraInfo* pInfo, s32 frame) {
        CameraTargetArg targetArg = CameraTargetArg();

        setCameraTargetToPlayer(&targetArg);

        return startMultiActorCameraTargetOther(pActor, pInfo, nullptr, targetArg, frame);
    }

    bool startActorCameraTargetSelf(const LiveActor* pActor, const ActorCameraInfo* pInfo, s32 frame) {
        CameraTargetArg targetArg = CameraTargetArg(pActor);

        return startMultiActorCameraTargetOther(pActor, pInfo, nullptr, targetArg, frame);
    }

    bool startActorCameraTargetOther(const LiveActor* pActor, const ActorCameraInfo* pInfo, const CameraTargetArg& rTargetArg, s32 frame) {
        return startMultiActorCameraTargetOther(pActor, pInfo, nullptr, rTargetArg, frame);
    }

    bool startMultiActorCameraNoTarget(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame) {
        CameraTargetArg targetArg = CameraTargetArg();

        return startMultiActorCameraTargetOther(pActor, pInfo, pEventName, targetArg, frame);
    }

    bool startMultiActorCameraTargetPlayer(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame) {
        CameraTargetArg targetArg = CameraTargetArg();

        setCameraTargetToPlayer(&targetArg);

        return startMultiActorCameraTargetOther(pActor, pInfo, pEventName, targetArg, frame);
    }

    bool startMultiActorCameraTargetSelf(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame) {
        return startMultiActorCameraTargetOther(pActor, pInfo, pEventName, CameraTargetArg(pActor), frame);
    }

    bool startMultiActorCameraTargetOther(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName,
                                          const CameraTargetArg& rTargetArg, s32 frame) {
        if (pInfo->mCameraSetID >= 0) {
            MR::FormattedName name;

            if (pEventName != nullptr) {
                ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
            } else {
                ::createActorCameraName(name, pActor, pInfo);
            }

            startEventCamera(pInfo, name.c_str(), rTargetArg, frame);

            return true;
        }

        return false;
    }

    void startAnimCameraTargetPlayer(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame, f32 speed) {
        CameraTargetArg targetArg;
        setCameraTargetToPlayer(&targetArg);

        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        startEventCameraAnim(pInfo, actorEventName.c_str(), targetArg, frame, speed);
    }

    void startAnimCameraTargetSelf(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame, f32 speed) {
        CameraTargetArg targetArg(pActor);

        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        startEventCameraAnim(pInfo, actorEventName.c_str(), targetArg, frame, speed);
    }

    void startAnimCameraTargetOther(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, const CameraTargetArg& rTargetArg,
                                    s32 frame, f32 speed) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        startEventCameraAnim(pInfo, actorEventName.c_str(), rTargetArg, frame, speed);
    }

    void startActorCameraProgrammable(const LiveActor* pActor, s32 frame) {
        startGlobalEventCameraNoTarget(pActor->getName(), frame);
    }

    bool endActorCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, bool resetView, s32 frame) {
        return endMultiActorCamera(pActor, pInfo, nullptr, resetView, frame);
    }

    bool endMultiActorCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, bool resetView, s32 frame) {
        if (pInfo->mCameraSetID >= 0) {
            MR::FormattedName name;

            if (pEventName != nullptr) {
                ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
            } else {
                ::createActorCameraName(name, pActor, pInfo);
            }

            endEventCamera(pInfo, name.c_str(), resetView, frame);

            return true;
        }

        return false;
    }

    bool endActorCameraAtLanding(const LiveActor* pActor, const ActorCameraInfo* pInfo, s32 frame) {
        return endMultiActorCameraAtLanding(pActor, pInfo, nullptr, frame);
    }

    bool endMultiActorCameraAtLanding(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame) {
        if (pInfo->mCameraSetID >= 0) {
            MR::FormattedName name;

            if (pEventName != nullptr) {
                ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
            } else {
                ::createActorCameraName(name, pActor, pInfo);
            }

            endEventCameraAtLanding(pInfo, name.c_str(), frame);

            return true;
        }

        return false;
    }

    void endActorCameraProgrammable(const LiveActor* pActor, s32 frame, bool resetView) {
        endGlobalEventCamera(pActor->getName(), frame, resetView);
    }

    bool isActiveMultiActorCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        if (pInfo->mCameraSetID < 0) {
            return false;
        }

        MR::FormattedName name;

        if (pEventName != nullptr) {
            ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
        } else {
            ::createActorCameraName(name, pActor, pInfo);
        }

        return isEventCameraActive(pInfo, name.c_str());
    }

    void setProgrammableCameraParam(const LiveActor* pActor, const Vec3f& rWPoint, const Vec3f& rPos, const Vec3f& rUpVec) {
        setProgrammableCameraParam(pActor->getName(), rWPoint, rPos, rUpVec, true);
    }

    void setProgrammableCameraParamFovy(const LiveActor* pActor, f32 fovy) {
        setProgrammableCameraParamFovy(pActor->getName(), fovy);
    }

    void initAnimCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pArcName, const char* pEventName) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        MR::FormattedName animName("%s.canm", pEventName);

        declareEventCameraAnim(pInfo, actorEventName.c_str(), loadResourceFromArc(pArcName, animName.c_str()));
    }

    void endAnimCamera(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName, s32 frame, bool resetView) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        endEventCamera(pInfo, actorEventName.c_str(), resetView, frame);
    }

    s32 getAnimCameraFrame(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        return getAnimCameraFrame(pInfo, actorEventName.c_str());
    }

    bool isAnimCameraEnd(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        MR::FormattedName actorEventName("%s%s", pActor->getName(), pEventName);

        return isAnimCameraEnd(pInfo, actorEventName.c_str());
    }

    s32 getActorCameraFrames(const LiveActor* pActor, const ActorCameraInfo* pInfo) {
        return getMultiActorCameraFrames(pActor, pInfo, nullptr);
    }

    s32 getMultiActorCameraFrames(const LiveActor* pActor, const ActorCameraInfo* pInfo, const char* pEventName) {
        if (pInfo->mCameraSetID >= 0) {
            MR::FormattedName name;

            if (pEventName != nullptr) {
                ::createMultiActorCameraName(name, pActor, pInfo, pEventName);
            } else {
                ::createActorCameraName(name, pActor, pInfo);
            }

            return getEventCameraFrames(pInfo, name.c_str());
        }

        return 0;
    }

    bool isExistActorCamera(const ActorCameraInfo* pInfo) {
        if (pInfo == nullptr) {
            return false;
        }

        return pInfo->mCameraSetID != -1;
    }

    void startRumbleWithShakeCameraWeak(const LiveActor* pActor, const char* pStrongPatternName, const char* pWeakPatternName, f32 strongDistMax,
                                        f32 weakDistMax) {
        f32 dist = calcDistanceToPlayer(pActor);

        if (dist <= strongDistMax) {
            tryRumblePad(pActor, pStrongPatternName, PLAYER_1);
            shakeCameraNormal();
        } else if (dist <= weakDistMax) {
            tryRumblePad(pActor, pWeakPatternName, PLAYER_1);
            shakeCameraWeak();
        }
    }

    void startRumbleWithShakeCameraNormalWeak(const LiveActor* pActor, const char* pStrongPatternName, const char* pWeakPatternName,
                                              f32 strongDistMax, f32 weakDistMax) {
        f32 dist = calcDistanceToPlayer(pActor);

        if (dist <= strongDistMax) {
            tryRumblePad(pActor, pStrongPatternName, PLAYER_1);
            shakeCameraNormalWeak();
        } else if (dist <= weakDistMax) {
            tryRumblePad(pActor, pWeakPatternName, PLAYER_1);
            shakeCameraWeak();
        }
    }

    void startRumbleWithShakeCameraStrong(const LiveActor* pActor, const char* pStrongPatternName, const char* pWeakPatternName, f32 strongDistMax,
                                          f32 weakDistMax) {
        f32 dist = calcDistanceToPlayer(pActor);

        if (dist <= strongDistMax) {
            tryRumblePad(pActor, pStrongPatternName, PLAYER_1);
            shakeCameraStrong();
        } else if (dist <= weakDistMax) {
            tryRumblePad(pActor, pWeakPatternName, PLAYER_1);
            shakeCameraNormal();
        }
    }
};  // namespace MR
