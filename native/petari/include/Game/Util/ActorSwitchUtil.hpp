#pragma once

class LiveActor;
class TableIter;

namespace MR {
    class FunctorBase;

    bool useStageSwitchReadA(LiveActor*, const TableIter&);
    bool useStageSwitchReadB(LiveActor*, const TableIter&);
    bool useStageSwitchReadAppear(LiveActor*, const TableIter&);
    void useStageSwitchSleep(LiveActor*, const TableIter&);
    bool useStageSwitchWriteA(LiveActor*, const TableIter&);
    bool useStageSwitchWriteB(LiveActor*, const TableIter&);
    bool useStageSwitchWriteDead(LiveActor*, const TableIter&);
    bool needStageSwitchReadA(LiveActor*, const TableIter&);
    bool needStageSwitchReadB(LiveActor*, const TableIter&);
    bool needStageSwitchReadAppear(LiveActor*, const TableIter&);
    bool needStageSwitchWriteA(LiveActor*, const TableIter&);
    bool needStageSwitchWriteB(LiveActor*, const TableIter&);
    bool needStageSwitchWriteDead(LiveActor*, const TableIter&);
    bool isValidSwitchA(const LiveActor*);
    bool isValidSwitchB(const LiveActor*);
    bool isValidSwitchAppear(const LiveActor*);
    bool isValidSwitchDead(const LiveActor*);
    bool isOnSwitchA(const LiveActor*);
    bool isOnSwitchB(const LiveActor*);
    bool isOnSwitchAppear(const LiveActor*);
    void onSwitchA(LiveActor*);
    void onSwitchB(LiveActor*);
    void onSwitchDead(LiveActor*);
    void offSwitchA(LiveActor*);
    void offSwitchB(LiveActor*);
    void offSwitchDead(LiveActor*);
    void syncStageSwitchAppear(LiveActor*);
    void listenStageSwitchOnAppear(LiveActor*, const MR::FunctorBase&);
    void listenStageSwitchOnOffAppear(LiveActor*, const MR::FunctorBase&, const MR::FunctorBase&);
    void listenStageSwitchOnA(LiveActor*, const MR::FunctorBase&);
    void listenStageSwitchOnOffA(LiveActor*, const MR::FunctorBase&, const MR::FunctorBase&);
    void listenStageSwitchOnB(LiveActor*, const MR::FunctorBase&);
    void listenStageSwitchOffB(LiveActor*, const MR::FunctorBase&);
    void listenStageSwitchOnOffB(LiveActor*, const MR::FunctorBase&, const MR::FunctorBase&);
};  // namespace MR
