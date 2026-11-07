#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class NameObj;
class Coin;
class CoinHostInfo;

class CoinHolder : public DeriveActorGroup< Coin > {
public:
    CoinHolder(const char* pName);

    virtual ~CoinHolder() {
    }

    virtual void init(const TableIter& rIter);

    bool hopCoin(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity);
    bool appearCoinFix(const NameObj* pObj, const Vec3f& rPosition, s32 count);
    bool appearCoinPop(const NameObj* pObj, const Vec3f& rPosition, s32 count);
    bool appearCoinPopToDirection(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity, s32 count);
    bool appearCoinToVelocity(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity, s32 count);
    bool appearCoinCircle(const NameObj* pObj, const Vec3f& rPosition, s32 count);
    CoinHostInfo* declare(const NameObj* pObj, s32 count);
    s32 getDeclareRemnantCoinCount(const NameObj* pObj) const;
    CoinHostInfo* findHostInfo(const NameObj* pObj) const;
    bool appearCoin(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity, s32 count, s32 life, s32 cannotTime, f32 randomize);

    Coin* takeDeadCoin();
    void createCoins();

    MR::Vector< MR::AssignableArray< CoinHostInfo* > > mHostInfos;
};

namespace MR {
    void createCoinHolder();
    void createCoinRotater();

    void addToCoinHolder(const NameObj* pObj, Coin* pCoin);
    CoinHolder* getCoinHolder();
};  // namespace MR
