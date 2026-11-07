#pragma once


class AlreadyDoneFlagInGalaxy;
class PlacementId;
class TableIter;

class GameDataTemporaryInGalaxy {
public:
    /// @brief Creates a new `GameDataTemporaryInGalaxy`.
    GameDataTemporaryInGalaxy();

    void resetPlayerRestartIdInfo();
    void setPlayerRestartIdInfo(const PlacementId&);
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
    s32 setupAlreadyDoneFlag(const char*, const TableIter&, u32*);
    void updateAlreadyDoneFlag(int, u32);
    void receiveStageResultParam(const char*, s32, int, int);

    PlacementId* mPlayerRestartIdInfo;
    bool _4;
    u32 _8;
    s16 mPowerStarId;
    char mStageName[128];
    int mStarPieceNum;
    int mLast1upStarPieceNum;
    int mCoinNum;
    u32 _4C;
    u32 _50;
    AlreadyDoneFlagInGalaxy* mAlreadyDoneFlag;
};
