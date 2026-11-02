#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class CocoNutBall;
class FireBall;
class FixedPosition;
class NameObjArchiveListCollector;

class OtaRock : public LiveActor {
public:
    /// @brief Creates a new `OtaRock`.
    /// @param pName A pointer to the null-terminated name of the object.
    OtaRock(const char* pName);

    virtual ~OtaRock();
    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void initModel();
    void initSensor();
    void updateBaseMtx();
    CocoNutBall* getDisappearedCocoNut();
    FireBall* getDisappearedFireBall();
    s32 getDisappearedCocoNutNum() const;
    s32 getDisappearedFireBallNum() const;
    bool isValidThrowCocoNut() const;
    bool isValidThrowFireBall() const;
    void throwCocoNut();
    void throwFireBall();
    bool tryToHide();

    void exeWait();
    void exeThrowCocoNut();
    void exeThrowFireBall();
    void exeDown();
    void exeHide();
    void exeHideWait();
    void exeShow();

private:
    bool mNoThrowCocoNut;
    CocoNutBall* mCocoNutArray;
    FireBall* mFireBallArray;
    FixedPosition* mFixedPosition;
    s32 mThrowCocoNutCounter;
    u32 mWaitFrame;
    TPos3f _A4;
    TVec3f _D4;
    AnimScaleController* mAnimScaleController;
};
