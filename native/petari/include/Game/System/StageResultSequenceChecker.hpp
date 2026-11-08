#pragma once


class GameDataHolder;

class StageResultSequenceCheckList {
    friend class StageResultSequenceChecker;

public:
    void init();

private:
    s32 mTicoGalaxyNum;
    s32 mPictureBookChapterCanRead;
    s32 mGalaxyNumCanOpen;
    s32 mPowerStarNum;
    s32 mGreenStarNum;
    bool mIsCompleteGalaxy;
};

class StageResultSequenceChecker {
public:
    StageResultSequenceChecker();

    void check();
    void checkIfComplete(const char*);
    bool isJustAppearTicoGalaxy() const;
    bool isAddPictureBook() const;
    bool isJustOpenLibraryRoom() const;
    bool isJustGetGreenStarFirst() const;
    bool isJustCompleteGalaxy() const;
    s32 getPrevPowerStarNum() const;
    s32 getAfterPowerStarNum() const;
    static void fillCheckList(StageResultSequenceCheckList*, const GameDataHolder*);
    static void fillCheckListIfComplete(StageResultSequenceCheckList*, const GameDataHolder*, const char*);
    bool isJustOpenGalaxyWithoutChallengeGalaxy() const;

private:
    StageResultSequenceCheckList mCheckListPrev;
    StageResultSequenceCheckList mCheckListAfter;
};
