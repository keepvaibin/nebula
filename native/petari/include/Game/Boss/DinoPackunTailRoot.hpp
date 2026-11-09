#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Util/JointController.hpp"

class DinoPackunTailRoot : public DinoPackunTailNode {
public:
    DinoPackunTailRoot(const char*, DinoPackun*);

    virtual void init(const TableIter&) override;
    virtual void addNodeVelocity(const Vec3f&) override;
    virtual JointController* createJointControllerOwn(LiveActor*, const char*) override;

    bool calcJointDirection(Pos3f*, const JointControllerInfo&);
};
