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

    virtual void init(const JMapInfoIter& rIter);

    void syncCollision();
    void updateTriangle();
    void updateCollisionHeader();
    void createCollision();

    KCLFile* mKCLFile;
    CollisionParts* mParts;
    u16 _94;
    u16 mPositionNum;
    TVec3f* mPositions;
    TVec3f* _9C;
    TriangleIndexing* mIndices;
    u32 _A4;
    u32 _A8;
    u32 _AC;
    u8 _B0[0x6C];
    u32 _11C;
};
