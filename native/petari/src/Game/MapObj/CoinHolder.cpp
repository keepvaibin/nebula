#include "Game/MapObj/CoinHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"

namespace {
    const s32 sCreateCoinNum = 32;
    const f32 sRandomizeVel = 4.0f;
    const f32 sCoinVelVertical = 25.0f;
    const f32 sCircleVelHRatio = 0.25f;
    const f32 sCircleVelLength = 30.0f;
};  // namespace

CoinHolder::CoinHolder(const char* pName) : DeriveActorGroup< Coin >(pName, ::sCreateCoinNum) {
    mHostInfos.init(::sCreateCoinNum);
}

bool CoinHolder::hopCoin(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rDirection) {
    CoinHostInfo* hostInfo = findHostInfo(pObj);

    // An object that declared no coins hops none.
    if (hostInfo == nullptr || hostInfo->mAppeared >= hostInfo->mDeclared) {
        return false;
    }

    Coin* coin = takeDeadCoin();

    if (coin != nullptr) {
        coin->setHostInfo(hostInfo);
        coin->appearHop(rPosition, rDirection);
        return true;
    }

    return false;
}

bool CoinHolder::appearCoinFix(const NameObj* pObj, const Vec3f& rPosition, s32 count) {
    Vec3f velocity(0.0f, 0.0f, 0.0f);

    return appearCoin(pObj, rPosition, velocity, count, -1, -1, count == 1 ? 0.0f : ::sRandomizeVel);
}

bool CoinHolder::appearCoinPop(const NameObj* pObj, const Vec3f& rPosition, s32 count) {
    Vec3f gravity;
    MR::calcGravityVector(this, rPosition, &gravity, nullptr, 0);
    Vec3f velocity = -gravity * ::sCoinVelVertical;

    return appearCoin(pObj, rPosition, velocity, count, -1, -1, count == 1 ? 0.0f : ::sRandomizeVel);
}

bool CoinHolder::appearCoinPopToDirection(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rDirection, s32 count) {
    Vec3f direction;
    MR::normalize(rDirection, &direction);

    return appearCoin(pObj, rPosition, direction * ::sCoinVelVertical, count, -1, -1, count == 1 ? 0.0f : ::sRandomizeVel);
}

bool CoinHolder::appearCoinToVelocity(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity, s32 count) {
    return appearCoin(pObj, rPosition, rVelocity, count, -1, -1, count == 1 ? 0.0f : ::sRandomizeVel);
}

bool CoinHolder::appearCoinCircle(const NameObj* pObj, const Vec3f& rPosition, s32 count) {
    if (count == 1) {
        return appearCoinPop(pObj, rPosition, count);
    }

    bool appeared = false;
    Vec3f gravity;
    MR::calcGravityVector(this, rPosition, &gravity, nullptr, 0);
    Vec3f axis;
    MR::makeAxisVerticalZX(&axis, gravity);

    for (s32 i = 0; i < count; i++) {
        f32 angle = 360.0f / count;

        Vec3f horizontal;
        MR::rotateVecDegree(&horizontal, axis, gravity, i * angle);
        horizontal.setLength(::sCircleVelHRatio);
        Vec3f velocity(horizontal - gravity);
        velocity.setLength(::sCircleVelLength);

        appeared |= appearCoin(pObj, rPosition, velocity, 1, -1, -1, 0.0f);
    }

    return appeared;
}

CoinHostInfo* CoinHolder::declare(const NameObj* pObj, s32 count) {
    if (count <= 0) {
        return nullptr;
    }

    CoinHostInfo* hostInfo = findHostInfo(pObj);
    if (hostInfo == nullptr) {
        hostInfo = new CoinHostInfo();
        hostInfo->mHostActor = pObj;
        mHostInfos.push_back(hostInfo);
    }

    hostInfo->mDeclared += count;
    return hostInfo;
}

s32 CoinHolder::getDeclareRemnantCoinCount(const NameObj* pObj) const {
    CoinHostInfo* hostInfo = findHostInfo(pObj);

    if (hostInfo == nullptr || MR::isGalaxyDarkCometAppearInCurrentStage()) {
        return 0;
    }

    return hostInfo->mDeclared - hostInfo->mAppeared;
}

CoinHostInfo* CoinHolder::findHostInfo(const NameObj* pObj) const {
    for (s32 i = 0; i < mHostInfos.size(); i++) {
        if (mHostInfos[i]->mHostActor == pObj) {
            return mHostInfos[i];
        }
    }

    return nullptr;
}

void CoinHolder::init(const TableIter& /*rIter*/) {
    createCoins();
}

Coin* CoinHolder::takeDeadCoin() {
    Coin* pCoin = getDeadMember();
    if (pCoin == nullptr) {
        const s32 first = getObjNum();
        createCoins();
        for (s32 i = first; i < getObjNum(); i++) {
            getMember(i)->initAfterPlacement();
        }
        pCoin = getDeadMember();
    }

    return pCoin;
}

void CoinHolder::createCoins() {
    NebulaArena::CurrentScope scope(Arenas::scene());
    for (int i = 0; i < ::sCreateCoinNum; i++) {
        Coin* pCoin = new Coin("コイン(共用)");
        pCoin->initWithoutIter();
        registerActor(pCoin);
    }
}

namespace MR {
    void createCoinHolder() {
        MR::createSceneObj(SceneObj_CoinRotater);
        MR::createSceneObj(SceneObj_CoinHolder);
    }

    CoinHolder* getCoinHolder() {
        return getSceneObj< CoinHolder >(SceneObj_CoinHolder);
    }

    void addToCoinHolder(const NameObj* pNameObj, Coin* pCoin) {
        pCoin->setHostInfo(getCoinHolder()->declare(pNameObj, 1));
    }
};  // namespace MR

bool CoinHolder::appearCoin(const NameObj* pObj, const Vec3f& rPosition, const Vec3f& rVelocity, s32 count, s32 life, s32 cannotTime,
                            f32 randomize) {
    CoinHostInfo* pHostInfo = findHostInfo(pObj);
    if (pHostInfo == nullptr) {
        return false;
    }

    bool appeared = false;
    for (s32 i = 0; i < count; i++) {
        if (pHostInfo->mAppeared >= pHostInfo->mDeclared) {
            break;
        }

        Coin* pCoin = takeDeadCoin();
        if (pCoin == nullptr) {
            break;
        }

        Vec3f velocity(rVelocity);
        if (!MR::isNearZero(randomize)) {
            MR::addRandomVector(&velocity, velocity, randomize);
        }

        pCoin->setHostInfo(pHostInfo);
        pCoin->appearMove(rPosition, velocity, life, cannotTime);
        appeared = true;
    }

    if (!MR::isGalaxyDarkCometAppearInCurrentStage() && appeared) {
        if (MR::hasME()) {
            MR::startSystemME("ME_COIN_APPEAR_S");
        } else {
            MR::startSystemSE("SE_SY_COIN_APPEAR_S");
        }
    }

    return appeared;
}
