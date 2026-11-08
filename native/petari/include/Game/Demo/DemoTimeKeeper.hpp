#pragma once


class DemoExecutor;

class DemoTimePartInfo {
public:
    DemoTimePartInfo();

    const char* mPartName;
    s32 mTotalStep;
    bool mSuspendFlag;
};

class DemoTimeKeeper {
public:
    DemoTimeKeeper(const DemoExecutor* pExecutor);

    void start();
    void update();
    void end();
    bool isDemoEnd() const;
    void setStartPart(const char* pPartName);
    bool isExistSuspendFlagCurrentPart() const;
    bool isPartLast() const;
    void setCurrentPart(const char* pPartName);
    bool isCurrentDemoPartLastStep() const;
    s32 getPart(const char* pPartName) const;

    const DemoExecutor* mExecutor;
    DemoTimePartInfo* mMainPartInfos;
    DemoTimePartInfo* mCurrentPart;  // in mMainPartInfos; nullptr when no demo runs
    s32 mNumPartInfos;
    s32 mElapsedStep;
    s32 mCurrentStep;
    s32 mPartIndex;
    bool mIsPaused;
};
