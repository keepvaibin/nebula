#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include <nebula_geometry.hpp>

class PlayerRacer;
class RaceManager;

enum RaceID {
    RaceID_Penguin,
    RaceID_TeresaPhantom,
    RaceID_TeresaDeathPromenade,
    RaceID_SurfingTrial,
    RaceID_SurfingChallenge,
};

class RaceManagerLayout : public LayoutActor {
public:
    RaceManagerLayout(const char* pName);

    /// @brief Intializes the `RaceManagerLayout` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter);

    void playCountAndGo();
    void playGo();
    void playGoal();
    void playLose();
    void playTimeUp();
    void playRank(u32);
    bool isAllAnimStopped();
    void hideRecordPane();
    void hideBestRecordPane();
    void hideAllPane();
    bool isPlayCountAnim() const;
    bool isPlayCountAndGoAnim() const;
    void setTime(u32);
    void setBestTime(u32);
};

class AbstractRacer {
public:
    AbstractRacer() {
        initRacer();
    }

    virtual void initRacer() {
        mRailCoord = 0.0f;
        mCurrPosition.zero();
        mPrevPosition.zero();
    }
    virtual void prepRacer(const RaceManager* pRaceManager) = 0;
    virtual void startRacer() = 0;
    virtual bool updateRacer(const RaceManager* pRaceManager) = 0;
    virtual bool goalRacer() = 0;
    virtual void loseRacer() = 0;
    virtual void resetRacer(const RaceManager* pRaceManager) = 0;
    virtual void exitRacer() = 0;

    static bool compRacer(const AbstractRacer* pRacerA, const AbstractRacer* pRacerB) {
        return pRacerA->mRailCoord > pRacerB->mRailCoord;
    }

    f32 mRailCoord;
    Vec3f mCurrPosition;
    Vec3f mPrevPosition;
};

class AbstractAudience {
public:
    AbstractAudience() {
    }

    virtual void prepAudience() = 0;
    virtual void resetAudience() = 0;
};

class RaceManager : public LiveActor {
public:
    RaceManager();

    /// @brief Intializes the `RaceManager` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter);

    virtual void movement();

    void exeWait();
    void exePrep();
    void exeWipeOut();
    void exeWipeWait();
    void exeWipeIn();
    void exeIntro();
    void exeCount();
    void exeRace();
    void exeDemo();
    void exeGoal();
    void exeRank();
    bool startWithWipe();
    bool startImmediately();
    void prepRace();
    void startRace();
    void goalRace();
    void resetRace();
    void prepBind();
    void entry(AbstractRacer*);
    void entry(PlayerRacer*);
    void entry(AbstractAudience*);
    bool isGoal(const AbstractRacer*) const;
    u32 getRank(const AbstractRacer*) const;
    void renewTime();

    s32 mCurrentRace;
    RaceManagerLayout* mLayout;
    MR::InlineArray< AbstractRacer*, 10 > mRacer;
    MR::InlineArray< AbstractAudience*, 10 > mAudience;
    u32 mRacerNum;
    u32 mAudienceNum;
    u32 mRank;
    u32 mBestTime;
    u32 mTime;
    PlayerRacer* mPlayerRacer;
    Pos3f mGoalInvMtx;
};

namespace RaceManagerFunction {
    void entryRacerOthers(AbstractRacer*);
    void entryAudience(AbstractAudience*);
    void entryRacerPlayer(PlayerRacer*);
    void startRaceWithWipe();
    void startRaceImmediately();
    u32 getRaceRank();
    u32 getRaceTime();
    const char* getRaceName(int);
    const char* getRaceMessageId(int);
    s32 getRaceId(const char*, s32);
    bool hasPowerStarRaceScenario(int);
};  // namespace RaceManagerFunction
