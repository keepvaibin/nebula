#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/LiveActorUtil.hpp"

// N made at init; when all are out, callEmptyBullet makes one more.
template < class T, s32 N >
class PressureBulletHolder : public NameObj {
public:
    PressureBulletHolder(const char* pName, const char* pBulletName) : NameObj(pName), mBullet(), mBulletName(pBulletName) {}

    virtual void init(const TableIter& rIter) {
        mBullet.init(N);
        for (s32 i = 0; i < N; i++) {
            createBullet();
        }
    }

    T* callEmptyBullet() {
        for (s32 i = 0; i < mBullet.size(); i++) {
            if (MR::isDead(mBullet[i])) {
                return mBullet[i];
            }
        }

        T* pBullet = createBullet();
        pBullet->initAfterPlacement();
        return pBullet;
    }

private:
    T* createBullet() {
        NebulaArena::CurrentScope scope(Arenas::scene());
        T* pBullet = new T(mBulletName);
        pBullet->initWithoutIter();
        mBullet.push_back(pBullet);
        return pBullet;
    }

    MR::Vector< MR::AssignableArray< T* > > mBullet;
    const char* mBulletName;
};
