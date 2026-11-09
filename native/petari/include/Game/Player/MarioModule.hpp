#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Mario;
class MarioActor;
class MarioAnimator;
class MarioState;
class Triangle;

// Animations whose "running" state Mario tracks by flag (Mario::mIndexedAnimeOn).
enum MarioIndexedAnime {
    MarioIndexedAnime_WallPush,         // 壁押し
    MarioIndexedAnime_WalkBrake,        // 歩行制動ブレーキ
    MarioIndexedAnime_SlideUpFaceDown,  // 坂すべり上向きうつぶせ
    MarioIndexedAnime_SlideDownFaceUp,  // 坂すべり下向きあおむけ
    MarioIndexedAnime_DiveReady,        // 飛び込み準備
    MarioIndexedAnime_Count
};

class MarioModule {
public:
    inline MarioModule(MarioActor* pActor) : mActor(pActor) {
    }

    virtual f32 getStickY() const;
    virtual void addVelocity(const Vec3f&);
    virtual void addVelocity(const Vec3f&, f32);

    MarioActor* getActor() const {
        return mActor;
    }

    Mario* getPlayer() const;
    MarioAnimator* getAnimator() const;
    Vec3f& getShadowPos() const;
    const Vec3f& getGravityVec() const;
    const Vec3f& getAirGravityVec() const;
    Vec3f& getFrontVec() const;
    bool isStatusActiveID(u32) const;
    bool isStatusActiveS(MarioState*) const;
    u16 getPlayerMode() const;
    bool isPlayerModeBee() const;
    bool isPlayerModeIce() const;
    bool isPlayerModeTeresa() const;
    bool isPlayerModeHopper() const;
    bool isPlayerModeInvincible() const;
    bool isPlayerModeFoo() const;
    void changeAnimation(const char*, const char* = nullptr);
    void changeAnimationNonStop(const char*);
    void changeAnimationWithAttr(const char*, u32);
    void stopAnimation(const char*, const char* = nullptr);
    bool isDefaultAnimationRun(const char*) const;
    void changeAnimationInterpoleFrame(u32);
    bool isAnimationRun(const char*) const;
    bool isAnimationTerminate(const char*) const;
    bool isAnimationTerminateUpper(const char*) const;
    const char* getAnimationStringPointer(const char*) const;
    const char* getCurrentBckName() const;
    void changeAnimationUpper(const char*, const char* = nullptr);
    void changeAnimationUpperWeak(const char*, const char* = nullptr);
    void stopAnimationUpper(const char*, const char* = nullptr);
    void stopAnimationUpperForce();
    f32 getAnimationFrame() const;
    void changeAnimation(const char*, MarioIndexedAnime);
    bool isAnimationRun(const char*, MarioIndexedAnime);
    void stopAnimation(const char*, MarioIndexedAnime);
    void setJointGlobalMtx(u32, MtxPtr);
    void setYangleOffset(f32);
    bool isStickOn() const;
    bool isStickFull() const;
    Vec3f& getWorldPadDir() const;
    bool calcWorldPadDir(Vec3f*, f32, f32, bool);
    void addVelocityAfter(const Vec3f&);
    void clearVelocity();
    Vec3f& getVelocity() const;
    void addTrans(const Vec3f&);
    f32 cutGravityElementFromJumpVec(bool);
    f32 cutVecElementFromJumpVec(const Vec3f&);
    Vec3f& getJumpVec() const;
    void setJumpVec(const Vec3f&);
    void playEffect(const char*);
    void playEffectTrans(const char*, const Vec3f&);
    void playEffectRT(const char*, const Vec3f&, const Vec3f&);
    void playEffectRTZ(const char*, const Vec3f&, const Vec3f&);
    void playEffectRTW(const char*, const Vec3f&, const Vec3f&);
    void playEffectSRT(const char*, f32, const Vec3f&, const Vec3f&);
    void stopEffect(const char*);
    void stopEffectForce(const char*);
    void playSound(const char*, s32 timing = -1);
    void stopSound(const char*, u32);
    void startCamVib(u32);
    Vec3f& getCamPos() const;
    Vec3f& getCamDirX() const;
    Vec3f& getCamDirY() const;
    Vec3f& getCamDirZ() const;
    void startPadVib(u32);
    void startPadVib(const char*);
    f32 getStickX() const;
    f32 getStickP() const;
    bool checkTrgA() const;
    bool checkLvlA() const;
    bool checkTrgZ() const;
    bool checkLvlZ() const;
    bool checkPreLvlZ() const;
    bool checkSquat(bool);
    Vec3f& getTrans() const;
    const Triangle* getGroundPolygon() const;
    bool isOnSlipGround() const;
    bool isSlipFloorCode(s32) const;
    bool isSlipPolygon(const Triangle*) const;
    u32 getFloorCode() const;
    f32 calcPolygonAngleD(const Triangle*) const;
    f32 calcAngleD(const Vec3f&) const;
    Triangle* getTmpPolygon() const;
    bool sendPunch(HitSensor*, bool);
    HitSensor* getSensor(const Triangle*) const;
    f32 marioAcos(f32) const;
    bool isInputDisable() const;

    MarioActor* mActor;
};

class MarioModuleTask {
public:
    virtual void init() = 0;
    virtual void start() = 0;
    virtual void update() = 0;
    virtual void calc() = 0;
    virtual void end() = 0;
    virtual bool exec() = 0;
    virtual void draw() const = 0;

    MarioModuleTask* mNext;
};
