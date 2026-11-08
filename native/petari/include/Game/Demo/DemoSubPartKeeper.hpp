#pragma once


class DemoExecutor;

class DemoSubPartInfo {
public:
    DemoSubPartInfo();

    const char* mSubPartName;
    s32 mSubPartTotalStep;
    const char* mMainPartName;
    s32 mMainPartStep;
    s32 mRestStep;
};

class DemoSubPartKeeper {
public:
    DemoSubPartKeeper(const DemoExecutor*);

    void update();
    void end();
    bool isDemoPartActive(const char*) const;
    s32 getDemoPartStep(const char*) const;
    s32 getDemoPartTotalStep(const char*) const;
    DemoSubPartInfo* findSubPart(const char*) const;

    const DemoExecutor* mExecutor;
    s32 mNumSubPartInfos;
    DemoSubPartInfo* mSubPartInfos;
};
