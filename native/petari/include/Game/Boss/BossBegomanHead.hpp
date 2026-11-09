#pragma once

#include "Game/LiveActor/PartsModel.hpp"
#include <nebula_geometry.hpp>

template < typename T >
class JointControlDelegator;
struct JointControllerInfo;

class BossBegomanHead : public PartsModel {
public:
    BossBegomanHead(LiveActor*, MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;

    bool isSwitchOn();
    bool isEdgeOut();
    void setOpeningDemo();
    void trySwitchPushTrample();
    void tryForceRecover();
    void tryTurn();
    void tryTurnEnd();
    void exeDemoWait();
    void exeOpeningDemo();
    void exeOnWait();
    void exeOffWait();
    void exeSwitchOn();
    void exeSwitchOff();
    void exeTurn();
    void exeTurnEnd();
    bool calcJointEdge(Pos3f*, const JointControllerInfo&);

private:
    f32 mAngle;
    JointControlDelegator< BossBegomanHead >* mJointDelegator;
};
