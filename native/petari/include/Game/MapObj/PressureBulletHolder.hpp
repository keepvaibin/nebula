#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/LiveActorUtil.hpp"

// A pool, as authored: how many bullets fly at once is part of the stages' design.
template < class T, s32 N >
class PressureBulletHolder : public NameObj {
public:
    PressureBulletHolder(const char* pName, const char* pBulletName) : NameObj(pName), mBullet(), mBulletName(pBulletName) {}

    virtual void init(const TableIter& rIter) {
        for (s32 i = 0; i < N; i++) {
            mBullet[i] = new T(mBulletName);
            mBullet[i]->initWithoutIter();
        }
    }

    // Null when all are out: the shot is skipped.
    T* callEmptyBullet() {
        for (s32 i = 0; i < N; i++) {
            if (MR::isDead(mBullet[i])) {
                return mBullet[i];
            }
        }

        return nullptr;
    }

private:
    T* mBullet[N];
    const char* mBulletName;
};
