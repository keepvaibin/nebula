#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Util/JointController.hpp"

class DinoPackunTailRoot : public DinoPackunTailNode {
public:
    DinoPackunTailRoot(const char*, DinoPackun*);

    virtual void init(const TableIter&);
    virtual void addNodeVelocity(const Vec3f&);
    virtual JointController* createJointControllerOwn(LiveActor*, const char*);

    bool calcJointDirection(Pos3f*, const JointControllerInfo&);
};
