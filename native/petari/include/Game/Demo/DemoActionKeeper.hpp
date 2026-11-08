#pragma once


class TableIter;
class LiveActor;
class Nerve;
class DemoExecutor;

namespace MR {
    class FunctorBase;
};  // namespace MR

class DemoActionInfo {
public:
    DemoActionInfo();

    void registerCast(LiveActor*);
    void registerFunctor(const LiveActor*, const MR::FunctorBase&);
    void registerNerve(const LiveActor*, const Nerve*);

    void executeActionFirst() const;
    void executeActionLast() const;

    const char* mPartName;
    const char* mCastName;
    s32 mCastID;
    s32 mActionType;
    const char* mPosName;
    const char* mAnimName;
    s32 mCastCapacity;
    s32 mCastCount;
    LiveActor** mCastList;
    MR::FunctorBase** mFunctors;
    const Nerve** mNerves;
};

class DemoActionKeeper {
public:
    DemoActionKeeper(const DemoExecutor*);

    void initCast(LiveActor*, const TableIter&);
    void registerFunctor(const LiveActor*, const MR::FunctorBase&, const char*);
    void registerNerve(const LiveActor*, const Nerve*, const char*);
    void update();
    bool isRegisteredDemoActionAppear(const LiveActor*) const;
    bool isRegisteredDemoActionFunctor(const LiveActor*) const;
    bool isRegisteredDemoActionNerve(const LiveActor*) const;
    bool isRegisteredDemoAction(const LiveActor*, s32) const;

    const DemoExecutor* mDemoExecutor;
    s32 mNumInfos;
    DemoActionInfo** mInfoArray;
};
