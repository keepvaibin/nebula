#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Mii/MiiHead.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include <nebula_geometry.hpp>

class FixedPosition;

// A Mii's head as an actor: its head built at the holder's draw (the first after it was made or its Mii changed), placed
// each frame from its own place or a joint it follows.
class MiiFaceParts : public LiveActor {
public:
    MiiFaceParts(const char* pName, const MiiFaceRecipe& rRecipe);

    virtual ~MiiFaceParts() override;
    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;

    void drawOpa(const Mii::Style& rStyle) const;
    void drawXlu(const Mii::Style& rStyle) const;
    void initFixedPosition(MtxPtr pJoint, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate);
    void changeExpressionNormal();
    void changeExpressionBlink();
    // The head of the Mii `rRecipe` names, built at the next draw.
    void changeFaceModel(const MiiFaceRecipe& rRecipe);
    // Builds the head when it waits to be (MiiFacePartsHolder at its draw).
    void buildIfWaiting();
    bool isWaitingToBuild() const { return mWaiting; }
    bool tryChangeExpression(Mii::Expression expression);

private:
    Mii::Head mHead;
    MiiFaceRecipe mRecipe;
    Pos3f mBaseMtx;
    MtxPtr mFollowMtx;
    FixedPosition* mFixedPosition;
    bool mWaiting;
};
