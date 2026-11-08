#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CameraCover;
class CameraDirector;
class CameraHolder;
class CameraMan;
class CameraManGame;
class CameraManEvent;
class CameraManSubjective;
class CameraParamChunkEvent;
class CameraParamChunkHolder;
class CameraPoseParam;
class CameraRailHolder;
class CameraRegisterHolder;
class CameraRotChecker;
class CameraTargetHolder;
class CameraShaker;
class CameraTargetArg;
class CameraTargetMtx;
class CameraTargetObj;
class CameraViewInterpolator;
class GameCameraCreator;
class LiveActor;
class MarioActor;
class OnlyCamera;

namespace MR {
    CameraDirector* getCameraDirector();
};  // namespace MR

struct CameraEvent {
    s32 mZoneID;
    const char* mName;  // MR::internName
    s32 mFrame;
};

class CameraDirector : public NameObj {
public:
    // Grows by doubling (CameraDirector::push).
    class CameraManStack {
    public:
        static const u32 cInitialCapacity = 8;

        CameraManStack() : mElements(new CameraMan*[cInitialCapacity]), mCapacity(cInitialCapacity), mCount(0) {
        }

        CameraMan** mElements;
        u32 mCapacity;
        u32 mCount;
    };

    CameraDirector(const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    void setTarget(CameraTargetObj*);
    CameraTargetObj* getTarget();
    void push(CameraMan*);
    CameraMan* pop();
    void backLastMtx();
    CameraMan* getCurrentCameraMan() const;
    void updateCameraMan();
    void calcPose();
    void calcSubjective();
    bool isInterpolationOff();
    void switchAntiOscillation();
    void createViewMtx();
    void checkStartCondition();
    void startEvent(s32, const char*, const CameraTargetArg&, s32);
    void endEvent(s32, const char*, bool, s32);
    void endEventAtLanding(s32, const char*, s32);
    CameraParamChunkEvent* getEventParameter(s32, const char*);
    void requestToResetCameraMan();
    void setInterpolation(u32);
    void cover(u32);
    void closeCreatingCameraChunk();
    void initCameraCodeCollection(const char*, s32);
    void registerCameraCode(u32);
    void termCameraCodeCollection();
    void declareEvent(s32, const char*);
    void started();
    void setTargetActor(const LiveActor*);
    void setTargetPlayer(const MarioActor*);
    bool isRotatingHard() const;
    bool isSubjectiveCamera() const;
    bool isEnableToControl() const;
    bool isEnableToRoundLeft() const;
    bool isEnableToRoundRight() const;
    bool isEnableToReset() const;
    bool isEventCameraActive(s32, const char*) const;
    bool isEventCameraActive() const;
    void startStartPosCamera(bool);
    bool isInterpolatingNearlyEnd() const;
    bool isForceCameraChange() const;
    f32 getDefaultFovy() const;
    void startStartAnimCamera();
    bool isStartAnimCameraEnd() const;
    u32 getStartAnimCameraFrame() const;
    void endStartAnimCamera();
    void startTalkCamera(const Vec3f&, const Vec3f&, f32, f32, s32);
    void endTalkCamera(bool, s32);
    void startSubjectiveCamera(s32);
    void endSubjectiveCamera(s32 endType);
    bool isAnimCameraEnd(s32, const char*) const;
    u32 getAnimCameraFrame(s32, const char*) const;
    void pauseOnAnimCamera(s32, const char*);
    void pauseOffAnimCamera(s32, const char*);
    void zoomInGameCamera();
    void zoomOutGameCamera();
    void checkEndOfEventCamera();
    void controlCameraSE();
    void removeEndEventAtLanding(s32, const char*);
    void calcViewMtxFromPoseParam(Pos3f*, const CameraPoseParam*);
    bool isPlayableCameraSE(bool);
    void resetCameraMan();
    void createStartAnimCamera();
    void createTalkCamera();
    void createSubjectiveCamera();

    CameraTargetObj* getTargetObj() const {
        return mTargetObj;
    }

    CameraHolder* getHolder() const {
        return mHolder;
    }

    void requestLocalOffsetReset() {
        mRequestCameraLocalOffsetReset = true;
    }

    CameraTargetObj* mTargetObj;
    CameraManStack* mStack;
    OnlyCamera* mOnlyCamera;
    CameraPoseParam* mPoseParam1;
    CameraPoseParam* mPoseParam2;
    CameraHolder* mHolder;
    CameraParamChunkHolder* mChunkHolder;
    GameCameraCreator* mCameraCreator;
    CameraRailHolder* mRailHolder;
    CameraRegisterHolder* mRegisterHolder;
    CameraTargetHolder* mTargetHolder;
    CameraShaker* mShaker;
    CameraViewInterpolator* mViewInterpolator;
    CameraCover* mCover;
    CameraRotChecker* mRotChecker;
    CameraManGame* mCameraManGame;
    CameraManEvent* mCameraManEvent;
    CameraManSubjective* mCameraManSubjective;
    MR::GrowTable< CameraEvent, 2 > mEvents;
    u32 mEventNum;
    bool mIsStartCameraActive;
    s32 mStartTime;
    bool mStartCameraCreated;
    CameraTargetMtx* mCameraTargetMtx;
    Pos3f mTargetMtx;
    bool mRequestCameraManReset;
    bool mRequestCameraLocalOffsetReset;
    bool mIsSubjectiveCamera;
    bool mIsStartSubjectiveCamera;
    s32 mSubjectiveFrame;
    f32 mNearZ;
    Pos3f mViewMtx;
    bool mIsSubjectiveCalced;
    bool mIsCameraNG;
};
