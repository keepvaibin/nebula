#pragma once


class AlreadyDoneFlagInGalaxy;
class JMapIdInfo;
class JMapInfoIter;

class GameDataTemporaryInGalaxy {
public:
    /// @brief Creates a new `GameDataTemporaryInGalaxy`.
    GameDataTemporaryInGalaxy();

    void resetPlayerRestartIdInfo();
    void setPlayerRestartIdInfo(const JMapIdInfo&);
    void resetRaceBestTime();
    bool isStageCleared() const;
    void resetStageResultPowerStarParam();
    void resetStageResultStarPieceParam();
    void resetStageResultCoinParam();
    void addStarPiece(int);
    void setLast1upStarPieceNum(int);
    s32 getStarPieceNum() const;
    void resetStageResultParam();
    void clearAlreadyDoneFlag();
    s32 setupAlreadyDoneFlag(const char*, const JMapInfoIter&, u32*);
    void updateAlreadyDoneFlag(int, u32);
    void receiveStageResultParam(const char*, s32, int, int);

    JMapIdInfo* mPlayerRestartIdInfo;
    bool _4;
    u32 _8;
    s16 mPowerStarId;
    char mStageName[48];
    int mStarPieceNum;
    int mLast1upStarPieceNum;
    int mCoinNum;
    u32 _4C;
    u32 _50;
    AlreadyDoneFlagInGalaxy* mAlreadyDoneFlag;
};
