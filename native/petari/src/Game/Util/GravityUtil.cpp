#include "Game/Util/GravityUtil.hpp"
#include "Game/Gravity/GravityInfo.hpp"
#include "Game/Gravity/PlanetGravity.hpp"
#include "Game/Gravity/PlanetGravityManager.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

namespace {
    const char cRange[] = "Range";
    const char cDistant[] = "Distant";
    const char cPriority[] = "Priority";
    const char cGravityType[] = "Gravity_type";
    const char cNormal[] = "Normal";
    const char cShadow[] = "Shadow";
    const char cMagnet[] = "Magnet";
    const char cPower[] = "Power";
    const char cLight[] = "Light";
    const char cHeavy[] = "Heavy";
    const char cInverse[] = "Inverse";

    PlanetGravityManager* getGravityManager() {
        return MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager);
    }

    void getPlacementArgPlus(const TableIter& rIter, const char* pFieldName, f32* pDest) {
        f32 value;

        if (rIter.getValue(pFieldName, &value) && value >= 0.0f) {
            *pDest = value;
        }
    }

    void getPlacementArgPlus(const TableIter& rIter, const char* pFieldName, s32* pDest) {
        s32 value;

        if (rIter.getValue(pFieldName, &value) && value >= 0) {
            *pDest = value;
        }
    }

    bool calcGravityVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, u32 typeFlags, Vec3f* pDest, GravityInfo* pInfo,
                                 const void* pHost) {
        if (pHost == nullptr) {
            pHost = pActor;
        }

        return getGravityManager()->calcTotalGravityVector(pDest, pInfo, rPosition, typeFlags, pHost);
    }
};  // namespace

namespace MR {
    void registerGravity(PlanetGravity* pGravity) {
        ::getGravityManager()->registerGravity(pGravity);
    }

    bool calcGravityVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcDropShadowVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityAndDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal | GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityAndMagnetVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal | GravityType_Magnet;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcDropShadowVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcGravityAndDropShadowVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_Normal | GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool calcAttractMarioLauncherOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost) {
        u32 typeFlags = GravityType_MarioLauncher;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, pHost);
    }

    bool isZeroGravity(const LiveActor* pActor) {
        Vec3f dummyGravity;
        return !::calcGravityVectorOrZero(pActor, pActor->mPosition, GravityType_Normal, &dummyGravity, nullptr, 0);
    }

    bool isLightGravity(const GravityInfo& rInfo) {
        PlanetGravity* pGravity = rInfo.mGravityInstance;

        if (pGravity == nullptr) {
            return false;
        }

        return pGravity->mGravityPower == GravityPower_Light;
    }

    void settingGravityParamFromPlacement(PlanetGravity* pGravity, const TableIter& rIter) {
        f32 range = pGravity->mRange;
        ::getPlacementArgPlus(rIter, ::cRange, &range);
        pGravity->mRange = range;

        f32 distant = pGravity->getDistant();
        ::getPlacementArgPlus(rIter, ::cDistant, &distant);
        pGravity->mDistant = distant;

        s32 priority = pGravity->mPriority;
        ::getPlacementArgPlus(rIter, ::cPriority, &priority);
        pGravity->setPriority(priority);

        getPlacementGravityType(rIter, pGravity);
        getPlacementGravityPower(rIter, pGravity);

        s32 inverse = pGravity->mIsInverse;
        ::getPlacementArgPlus(rIter, ::cInverse, &inverse);
        pGravity->mIsInverse = inverse;
    }

    void getPlacementGravityType(const TableIter& rIter, PlanetGravity* pGravity) {
        const char* pType = nullptr;

        if (rIter.getValue(::cGravityType, &pType)) {
            if (MR::isEqualString(pType, ::cNormal)) {
                pGravity->mGravityType = GravityType_Normal;
            } else if (MR::isEqualString(pType, ::cShadow)) {
                pGravity->mGravityType = GravityType_Shadow;
            } else if (MR::isEqualString(pType, ::cMagnet)) {
                pGravity->mGravityType = GravityType_Magnet;
            }
        }
    }

    void getPlacementGravityPower(const TableIter& rIter, PlanetGravity* pGravity) {
        const char* pPower = nullptr;

        if (rIter.getValue(::cPower, &pPower)) {
            if (MR::isEqualString(pPower, ::cLight)) {
                pGravity->mGravityPower = GravityPower_Light;
            } else if (MR::isEqualString(pPower, ::cNormal)) {
                pGravity->mGravityPower = GravityPower_Normal;
            } else if (MR::isEqualString(pPower, ::cHeavy)) {
                pGravity->mGravityPower = GravityPower_Heavy;
            }
        }
    }
};  // namespace MR
