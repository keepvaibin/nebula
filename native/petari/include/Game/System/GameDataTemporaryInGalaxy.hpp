#pragma once


class AlreadyDoneFlagInGalaxy;
class PlacementId;
class TableIter;

class GameDataTemporaryInGalaxy {
public:
    GameDataTemporaryInGalaxy();

    void resetPlayerRestartIdInfo();
    void setPlayerRestartIdInfo(const PlacementId&);
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
    bool mIsRetry;
    u32 mClearState;
    s32 mPowerStarId;
    const char* mStageName;  // MR::internName
    int mStarPieceNum;
    int mLast1upStarPieceNum;
    int mCoinNum;
    AlreadyDoneFlagInGalaxy* mAlreadyDoneFlag;
};
