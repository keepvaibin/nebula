#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
struct KCLFile;

class DynamicCollisionObj : public LiveActor {
public:
    class TriangleIndexing {
    public:
        inline TriangleIndexing() {
        }

        u16 mIndex[3];
    };

    DynamicCollisionObj(const char* pName);

    virtual ~DynamicCollisionObj() {
    }

    virtual void init(const TableIter& rIter);

    void syncCollision();
    void updateTriangle();
    void updateCollisionHeader();
    void createCollision();

    KCLFile* mKCLFile;
    CollisionParts* mParts;
    u16 mTriangleNum;
    u16 mPositionNum;
    Vec3f* mPositions;
    TriangleIndexing* mIndices;
};
