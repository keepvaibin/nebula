#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
struct KCLFile;

class DynamicCollisionObj : public LiveActor {
public:
    struct TriangleIndexing {
        u32 mIndex[3];
    };

    DynamicCollisionObj(const char* pName);

    virtual ~DynamicCollisionObj() override {
    }

    virtual void init(const TableIter& rIter) override;

    void syncCollision();
    void updateTriangle();
    void updateCollisionHeader();
    void createCollision();

    KCLFile* mKCLFile;
    CollisionParts* mParts;
    s32 mTriangleNum;
    s32 mPositionNum;
    Vec3f* mPositions;
    TriangleIndexing* mIndices;
};
