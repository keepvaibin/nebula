#include "Game/Util/Color.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Animation/BckCtrl.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/LiveActor/ActorAnimKeeper.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/MirrorActor.hpp"
#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/Flag.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/NameObj/NameObjFinder.hpp"
#include "Game/Player/GroupChecker.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include "Game/Util/FurMulti.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    CollisionParts* createCollisionParts(ResourceHolder* pResHolder, const char* pName, HitSensor* pSensor, const Pos3f& rMtx,
                                         MR::CollisionScaleType scaleType, s32 category) {
        CollisionParts* parts = new CollisionParts();
        MR::FormattedName kcl("%s.kcl", pName);
        MR::FormattedName pa("%s.pa", pName);
        void* kclRes;
        void* paRes = nullptr;
        kclRes = pResHolder->mFileInfoTable->getRes(kcl.c_str());

        if (pResHolder->mFileInfoTable->isExistRes(pa.c_str())) {
            paRes = pResHolder->mFileInfoTable->getRes(pa.c_str());
        }

        switch (scaleType) {
        case MR::CollisionScaleType_AutoEqualScale:
            parts->initWithAutoEqualScale(rMtx, pSensor, kclRes, paRes, category, false);
            break;
        case MR::CollisionScaleType_NotUsingScale:
            parts->initWithNotUsingScale(rMtx, pSensor, kclRes, paRes, category, false);
            break;
        case MR::CollisionScaleType_Default:
            parts->init(rMtx, pSensor, kclRes, paRes, category, false);
            break;
        }

        return parts;
    }

    const char* createSubModelObjName(const LiveActor* pActor, const char* pSubName) {
        // A null sub model name is the empty name.
        if (pSubName == nullptr) {
            pSubName = "";
        }
        u32 subLength = strlen(pSubName);
        u32 actorLength = strlen(pActor->mName);
        u32 length = actorLength + subLength + strlen("（）") + 1;
        char* pBuffer = new char[length];
        nebula_snprintf(pBuffer, length, "%s（%s）", pActor->mName, pSubName);
        return pBuffer;
    }

    PartsModel* createSubModel(LiveActor* pActor, const char* pSubModelName, MtxPtr pMtx, int drawBufferType) {
        const char* modelResName = MR::getModelResourceHolder(pActor)->mModelResTable->getResName(0u);

        if (!MR::isExistSubModel(modelResName, pSubModelName)) {
            return nullptr;
        }

        MR::FormattedName subModelName("%s%s", modelResName, pSubModelName);

        const char* objName = createSubModelObjName(pActor, pSubModelName);
        PartsModel* parts = new PartsModel(pActor, objName, subModelName.c_str(), pMtx, drawBufferType, false);
        parts->initWithoutIter();
        MR::tryStartAllAnim(parts, subModelName.c_str());
        return parts;
    }

    void changeBckForEffectKeeper(const LiveActor* pActor) {
        EffectKeeper* keeper = pActor->mEffectKeeper;

        if (keeper != nullptr) {
            keeper->changeBck();
        }
    }

    void calcCollisionMtx(Pos3f* pMtx, const LiveActor* pActor) {
        pMtx->set(pActor->getBaseMtx());

        f32 scale = pActor->mScale.x;
        pMtx->mMtx[0][0] *= scale;
        pMtx->mMtx[0][1] *= scale;
        pMtx->mMtx[0][2] *= scale;
        pMtx->mMtx[1][0] *= scale;
        pMtx->mMtx[1][1] *= scale;
        pMtx->mMtx[1][2] *= scale;
        pMtx->mMtx[2][0] *= scale;
        pMtx->mMtx[2][1] *= scale;
        pMtx->mMtx[2][2] *= scale;
    }

    bool isShowModel(LiveActor* pActor) {
        return !pActor->mFlag.mIsHiddenModel;
    }

    void callFuncAllGroupMember(const NameObj* pActor, void (*pFunc)(NameObj*)) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        LiveActorGroup* pGroup = pGroupArray->getLiveActorGroup(static_cast<const LiveActor*>(pActor));

        if (pGroup == nullptr) {
            return;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pMember = pGroup->getActor(i);

            if (pMember == pActor) {
                continue;
            }

            pFunc(pMember);
        }
    }

    void callFuncAllGroupMember(const LiveActor* pActor, void (*pFunc)(LiveActor*)) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        LiveActorGroup* pGroup = pGroupArray->getLiveActorGroup(pActor);

        if (pGroup == nullptr) {
            return;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pMember = pGroup->getActor(i);

            if (pMember == pActor) {
                continue;
            }

            pFunc(pMember);
        }
    }

    void callMethodAllGroupMember(const LiveActor* pActor, void (LiveActor::*pMethod)()) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        LiveActorGroup* pGroup = pGroupArray->getLiveActorGroup(pActor);

        if (pGroup == nullptr) {
            return;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pMember = pGroup->getActor(i);

            if (pMember == pActor) {
                continue;
            }

            (pMember->*pMethod)();
        }
    }

    template < typename T >
    s32 countGroupMember(const LiveActor* pActor, T pPred);

    template <>
    s32 countGroupMember< bool (*)(LiveActor*) >(const LiveActor* pActor, bool (*pPred)(LiveActor*)) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        LiveActorGroup* pGroup = pGroupArray->getLiveActorGroup(pActor);
        s32 count = 0;

        if (pGroup == nullptr) {
            return 0;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pMember = pGroup->getActor(i);

            if (pMember == pActor) {
                continue;
            }

            if (pPred(pMember)) {
                count++;
            }
        }

        return count;
    }

    template <>
    s32 countGroupMember< bool (*)(const LiveActor*) >(const LiveActor* pActor, bool (*pPred)(const LiveActor*)) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        LiveActorGroup* pGroup = pGroupArray->getLiveActorGroup(pActor);
        s32 count = 0;

        if (pGroup == nullptr) {
            return 0;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pMember = pGroup->getActor(i);

            if (pMember == pActor) {
                continue;
            }

            if (pPred(pMember)) {
                count++;
            }
        }

        return count;
    }
};  // namespace

namespace MR {
    bool isExistIndirectTexture(const LiveActor* pActor) {
        const char* name = "IndDummy";
        return MR::getModelData(pActor)->findTexture(name) != -1;
    }

    void initMirrorReflection(LiveActor* pActor) {
        pActor->mModelManager->prepareMaterials();
        changeModelDataTexAll(pActor, "MirrorTex", *MR::getScreenResTIMG());
        pActor->mModelManager->mMaterialUpdater->addMirrorReflectionMtxSetter();
    }

    CollisionParts* tryCreateCollisionMoveLimit(LiveActor* pActor, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "MoveLimit";
            kcl.format("%s.kcl", pKclName);
        }

        if (!MR::getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "MoveLimit";
        MR::makeMtxTRS(mtx, pActor);
        CollisionParts* parts = ::createCollisionParts(MR::getResourceHolder(pActor), pCollisionName, pSensor, mtx, MR::CollisionScaleType_Default, CollisionCategory_MoveLimit);

        if (parts != nullptr) {
            MR::validateCollisionParts(parts);
        }

        return parts;
    }

    CollisionParts* tryCreateCollisionWaterSurface(LiveActor* pActor, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "WaterSurface";
            kcl.format("%s.kcl", pKclName);
        }

        if (!MR::getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "WaterSurface";
        MR::makeMtxTRS(mtx, pActor);
        CollisionParts* parts = ::createCollisionParts(MR::getResourceHolder(pActor), pCollisionName, pSensor, mtx, MR::CollisionScaleType_Default, CollisionCategory_WaterSurface);

        if (parts != nullptr) {
            MR::validateCollisionParts(parts);
        }

        return parts;
    }

    CollisionParts* tryCreateCollisionSunshade(LiveActor* pActor, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "Sunshade";
            kcl.format("%s.kcl", pKclName);
        }

        if (!MR::getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "Sunshade";
        MR::makeMtxTRS(mtx, pActor);
        CollisionParts* parts = ::createCollisionParts(MR::getResourceHolder(pActor), pCollisionName, pSensor, mtx, MR::CollisionScaleType_Default, CollisionCategory_Sunshade);

        if (parts != nullptr) {
            MR::validateCollisionParts(parts);
        }

        return parts;
    }

    PartsModel* createBloomModel(LiveActor* pActor, MtxPtr pMtx) {
        PartsModel* parts = ::createSubModel(pActor, "Bloom", pMtx, MR::DrawBufferType_BloomModel);

        if (parts != nullptr) {
            MR::registerDemoSimpleCastAll(parts);
        }

        return parts;
    }

    PartsModel* createWaterModel(LiveActor* pActor, MtxPtr pMtx) {
        return ::createSubModel(pActor, "Water", pMtx, MR::DrawBufferType_MapObj);
    }

    PartsModel* createIndirectPlanetModel(LiveActor* pActor, MtxPtr pMtx) {
        return ::createSubModel(pActor, "Indirect", pMtx, MR::DrawBufferType_IndirectPlanet);
    }

    MirrorActor* tryCreateMirrorActor(LiveActor* pActor, const char* pModelName) {
        if (!MR::isInAreaObj("MirrorArea", pActor->mPosition)) {
            return nullptr;
        }

        const char* objName = ::createSubModelObjName(pActor, "鏡内モデル");
        MirrorActor* mirror = new MirrorActor(pActor, objName, pModelName);
        mirror->initWithoutIter();
        return mirror;
    }

    bool isOnGround(const LiveActor* pActor) {
        Binder* binder = pActor->mBinder;

        if (binder == nullptr) {
            return false;
        }

        if (!binder->isBindedGround()) {
            return false;
        }

        const Vec3f* normal = binder->mGroundInfo.mParentTriangle.getNormal(0);
        return !(0.0f < pActor->mVelocity.dot(*normal));
    }

    bool isOnGroundCos(const LiveActor* pActor, f32 cos) {
        bool isGround = false;

        if (isOnGround(pActor)) {
            const Vec3f* normal = pActor->mBinder->mGroundInfo.mParentTriangle.getNormal(0);

            if (MR::isFloorPolygonCos(*normal, pActor->mGravity, cos)) {
                isGround = true;
            }
        }

        return isGround;
    }

    bool isBindedGround(const LiveActor* pActor) {
        Binder* binder = pActor->mBinder;

        if (binder == nullptr) {
            return false;
        }

        return binder->isBindedGround();
    }

    bool isBindedGround(const LiveActor* pActor, HitSensor* pSensor) {
        if (isBindedGround(pActor)) {
            return pSensor == MR::getGroundSensor(pActor);
        }

        return false;
    }

    bool isBindedWall(const LiveActor* pActor) {
        Binder* binder = pActor->mBinder;

        if (binder == nullptr) {
            return false;
        }

        return binder->isBindedWall();
    }

    bool isBindedWall(const LiveActor* pActor, HitSensor* pSensor) {
        if (isBindedWall(pActor)) {
            return pSensor == MR::getWallSensor(pActor);
        }

        return false;
    }

    bool isBindedWallOfMap(const LiveActor* pActor) {
        bool isMap = false;

        if (isBindedWall(pActor)) {
            const CollisionParts* parts = pActor->mBinder->mWallInfo.mParentTriangle.mParts;

            if (parts->mKeeperIndex == CollisionCategory_Map) {
                isMap = true;
            }
        }

        return isMap;
    }

    bool isBindedWallOfMoveLimit(const LiveActor* pActor) {
        bool isMoveLimit = false;

        if (isBindedWall(pActor)) {
            const CollisionParts* parts = pActor->mBinder->mWallInfo.mParentTriangle.mParts;

            if (parts->mKeeperIndex == CollisionCategory_MoveLimit) {
                isMoveLimit = true;
            }
        }

        return isMoveLimit;
    }

    bool isBindedWallOrSlopeGround(const LiveActor* pActor, f32 cos, Vec3f* pNormal) {
        if (isBindedWall(pActor)) {
            if (pNormal != nullptr) {
                const Vec3f* normal = pActor->mBinder->mWallInfo.mParentTriangle.getNormal(0);
                pNormal->set(*normal);
            }

            return true;
        }

        if (isOnGround(pActor)) {
            const Vec3f* normal = pActor->mBinder->mGroundInfo.mParentTriangle.getNormal(0);

            if (!MR::isFloorPolygonCos(*normal, pActor->mGravity, cos)) {
                if (pNormal != nullptr) {
                    const Vec3f* groundNormal = pActor->mBinder->mGroundInfo.mParentTriangle.getNormal(0);
                    pNormal->set(*groundNormal);
                }

                return true;
            }
        }

        return false;
    }

    bool isBindedRoof(const LiveActor* pActor) {
        if (pActor->mBinder == nullptr) {
            return false;
        }

        return pActor->mBinder->isBindedRoof();
    }

    bool isBindedRoof(const LiveActor* pActor, HitSensor* pSensor) {
        if (isBindedRoof(pActor)) {
            return pSensor == MR::getRoofSensor(pActor);
        }

        return false;
    }

    bool isBinded(const LiveActor* pActor) {
        if (isBindedGround(pActor) || isBindedRoof(pActor) || isBindedWall(pActor)) {
            return true;
        }

        return false;
    }

    bool isBinded(const LiveActor* pActor, HitSensor* pSensor) {
        if (isBindedGround(pActor, pSensor) || isBindedRoof(pActor, pSensor) || isBindedWall(pActor, pSensor)) {
            return true;
        }

        return false;
    }

    bool isPressedRoofAndGround(const LiveActor* pActor) {
        if (!isBindedRoof(pActor) || !isBindedGround(pActor)) {
            return false;
        }

        if (MR::isSensorPressObj(MR::getGroundSensor(pActor)) || MR::isSensorPressObj(MR::getRoofSensor(pActor))) {
            const Binder* binder = pActor->mBinder;
            const HitInfo* pRoofInfo = &binder->mRoofInfo;
            const HitInfo* pGroundInfo = &binder->mGroundInfo;
            Vec3f roofPower;
            Vec3f groundPower;
            pRoofInfo->mParentTriangle.calcForceMovePower(&roofPower, pRoofInfo->mHitPos);
            pGroundInfo->mParentTriangle.calcForceMovePower(&groundPower, pGroundInfo->mHitPos);

            if (0.0f < (roofPower - groundPower).dot(pActor->mGravity)) {
                return true;
            }
        }

        return false;
    }

    bool isPressedMovingWall(const LiveActor* pActor) {
        if (!isBindedWall(pActor)) {
            return false;
        }

        s32 planeNum = pActor->mBinder->mPlaneNum;

        for (s32 i = 0; i < planeNum - 1; i++) {
            const HitInfo* plane1 = pActor->mBinder->getPlane(i);

            if (!MR::isWallPolygon(*plane1->mParentTriangle.getFaceNormal(), pActor->mGravity)) {
                continue;
            }

            Vec3f power1;
            plane1->mParentTriangle.calcForceMovePower(&power1, plane1->mHitPos);

            for (s32 j = i + 1; j < planeNum; j++) {
                const HitInfo* plane2 = pActor->mBinder->getPlane(j);

                if (!MR::isWallPolygon(*plane2->mParentTriangle.getFaceNormal(), pActor->mGravity)) {
                    continue;
                }

                if (0.0f <= plane1->mParentTriangle.getFaceNormal()->dot(*plane2->mParentTriangle.getFaceNormal())) {
                    continue;
                }

                Vec3f power2;
                plane2->mParentTriangle.calcForceMovePower(&power2, plane2->mHitPos);

                Vec3f diffHit(plane1->mHitPos);
                diffHit.sub(plane2->mHitPos);

                Vec3f diffPower(power2);
                diffPower.sub(power1);

                if (0.0f < diffPower.dot(diffHit)) {
                    return true;
                }
            }
        }

        return false;
    }

    void copyTransRotateScale(const LiveActor* pSrc, LiveActor* pDst) {
        pDst->mPosition.set(pSrc->mPosition);
        pDst->mRotation.set(pSrc->mRotation);
        pDst->mScale.set(pSrc->mScale);
    }

    void initDefaultPos(LiveActor* pActor, const TableIter& rIter) {
        if (!rIter.isValid()) {
            return;
        }

        MR::getPlacementTrans(rIter, &pActor->mPosition);
        MR::getPlacementRotate(rIter, &pActor->mRotation);
        MR::getPlacementScale(rIter, &pActor->mScale);

        pActor->mRotation.x = MR::repeat(pActor->mRotation.x, 0.0f, 360.0f);
        pActor->mRotation.y = MR::repeat(pActor->mRotation.y, 0.0f, 360.0f);
        pActor->mRotation.z = MR::repeat(pActor->mRotation.z, 0.0f, 360.0f);
    }

    void initDefaultPosNoRepeat(LiveActor* pActor, const TableIter& rIter) {
        MR::getPlacementTrans(rIter, &pActor->mPosition);
        MR::getPlacementRotate(rIter, &pActor->mRotation);
        MR::getPlacementScale(rIter, &pActor->mScale);
    }

    bool isValidMovement(const LiveActor* pActor) {
        if (pActor->mFlag.mIsDead || pActor->mFlag.mIsClipped) {
            return false;
        }

        return true;
    }

    bool isValidCalcAnim(const LiveActor* pActor) {
        if (pActor->mFlag.mIsDead || pActor->mFlag.mIsClipped || pActor->mFlag.mIsNoCalcAnim) {
            return false;
        }

        return true;
    }

    bool isValidDraw(const LiveActor* pActor) {
        if (pActor->mFlag.mIsDead || pActor->mFlag.mIsClipped || pActor->mFlag.mIsHiddenModel) {
            return false;
        }

        return true;
    }

    void calcAnimDirect(LiveActor* pActor) {
        bool isNoCalcAnim = pActor->mFlag.mIsNoCalcAnim;

        pActor->mFlag.mIsNoCalcAnim = false;
        pActor->calcAnim();

        if (isNoCalcAnim) {
            pActor->mFlag.mIsNoCalcAnim = true;
        }
    }

    void setClippingTypeSphere(LiveActor* pActor, f32 radius) {
        MR::getClippingDirector()->mActorHolder->setTypeToSphere(pActor, radius, nullptr);
    }

    void setClippingTypeSphere(LiveActor* pActor, f32 radius, const Vec3f* pTrans) {
        MR::getClippingDirector()->mActorHolder->setTypeToSphere(pActor, radius, pTrans);
    }

    void setClippingTypeSphereContainsModelBoundingBox(LiveActor* pActor, f32 radiusOffset) {
        f32 modelBoundingRadius = 0.0f;
        MR::calcModelBoundingRadius(&modelBoundingRadius, pActor);

        setClippingTypeSphere(pActor, modelBoundingRadius + radiusOffset, nullptr);
    }

    void setClippingFar50m(LiveActor* pActor) {
        MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 7);
    }

    void setClippingFar100m(LiveActor* pActor) {
        MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 6);
    }

    void setClippingFar200m(LiveActor* pActor) {
        MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 5);
    }

    void setClippingFar300m(LiveActor* pActor) {
        MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 4);
    }

    void setClippingFarMax(LiveActor* pActor) {
        MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 0);
    }

    void setClippingFar(LiveActor* pActor, f32 clipping) {
        s32 clip = clipping;

        switch (clip) {
        case 50:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 7);
            break;
        case 100:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 6);
            break;
        case 200:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 5);
            break;
        case 300:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 4);
            break;
        case 400:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 3);
            break;
        case 500:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 2);
            break;
        case 600:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 1);
            break;
        case -1:
            MR::getClippingDirector()->mActorHolder->setFarClipLevel(pActor, 0);
            break;
        }
    }

    void setGroupClipping(LiveActor* pActor, const TableIter& rIter, int capacity) {
        MR::getClippingDirector()->joinToGroupClipping(pActor, rIter, capacity);
    }

    void validateClipping(LiveActor* pActor) {
        if (pActor->mFlag.mIsInvalidClipping) {
            MR::getClippingDirector()->mActorHolder->validateClipping(pActor);
        }
    }

    void invalidateClipping(LiveActor* pActor) {
        if (pActor->mFlag.mIsInvalidClipping) {
            if (pActor->mFlag.mIsClipped) {
                pActor->endClipped();
            }
        } else {
            MR::getClippingDirector()->mActorHolder->invalidateClipping(pActor);
        }
    }

    bool changeShowModelFlagSyncNearClipping(LiveActor* pActor, f32 nearClip) {
        if (MR::isJudgedToNearClip(pActor->mPosition, nearClip)) {
            MR::hideModelAndOnCalcAnimIfShown(pActor);
            return false;
        }

        MR::showModelIfHidden(pActor);
        return true;
    }

    bool isClipped(const LiveActor* pActor) {
        return pActor->mFlag.mIsClipped;
    }

    bool isInvalidClipping(const LiveActor* pActor) {
        return pActor->mFlag.mIsInvalidClipping;
    }

    void setBaseTRMtx(LiveActor* pActor, MtxPtr pMtx) {
        NebulaMtxCopy(pMtx, getModel(pActor)->mBaseTransformMtx);
    }

    void setBaseTRMtx(LiveActor* pActor, const Pos3f& rMtx) {
        NebulaMtxCopy(rMtx.mMtx, getModel(pActor)->mBaseTransformMtx);
    }

    void setBaseTRMtx(LiveActor* pActor, const Quat4f& rQuat) {
        Pos3f mtx;
        mtx.setQT(rQuat, pActor->mPosition);

        NebulaMtxCopy(mtx.mMtx, getModel(pActor)->mBaseTransformMtx);
    }

    void setBaseScale(LiveActor* pActor, const Vec3f& rScale) {
        Model* pModel = getModel(pActor);
        pModel->mBaseScale.x = rScale.x;
        pModel->mBaseScale.y = rScale.y;
        pModel->mBaseScale.z = rScale.z;
    }

    ResourceHolder* getResourceHolder(const LiveActor* pActor) {
        if (pActor->mModelManager != nullptr) {
            return pActor->mModelManager->getResourceHolder();
        }

        return nullptr;
    }

    ResourceHolder* getModelResourceHolder(const LiveActor* pActor) {
        if (pActor->mModelManager != nullptr) {
            return pActor->mModelManager->getModelResourceHolder();
        }

        return nullptr;
    }

    ResTIMG* getTexFromModel(const char* pName, const LiveActor* pActor) {
        s32 texIndex = getModelData(pActor)->findTexture(pName);
        return getModelData(pActor)->getTexture(texIndex);
    }

    ResTIMG* getTexFromArc(const char* pName, const LiveActor* pActor) {
        return (ResTIMG*)getResourceHolder(pActor)->mFileInfoTable->getRes(pName);
    }

    const char* getModelResName(const LiveActor* pActor) {
        return getModelResourceHolder(pActor)->mModelResTable->getResName(0u);
    }

    bool isExistAnim(const LiveActor* pActor, const char* pName) {
        if (isExistBck(pActor, pName)) {
            return true;
        }

        if (isExistBtk(pActor, pName)) {
            return true;
        }

        if (isExistBrk(pActor, pName)) {
            return true;
        }

        if (isExistBtp(pActor, pName)) {
            return true;
        }

        if (isExistBpk(pActor, pName)) {
            return true;
        }

        return isExistBva(pActor, pName);
    }

    bool isExistBck(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mMotionResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mMotionResTable->isExistRes(pName);
    }

    bool isExistBtk(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mBtkResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mBtkResTable->isExistRes(pName);
    }

    bool isExistBrk(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mBrkResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mBrkResTable->isExistRes(pName);
    }

    bool isExistBtp(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mBtpResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mBtpResTable->isExistRes(pName);
    }

    bool isExistBpk(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mBpkResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mBpkResTable->isExistRes(pName);
    }

    bool isExistBva(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return getResourceHolder(pActor)->mBvaResTable->mCount != 0;
        }

        return getResourceHolder(pActor)->mBvaResTable->isExistRes(pName);
    }

    bool isExistTexture(const LiveActor* pActor, const char* pName) {
        return getModelData(pActor)->findTexture(pName) != -1;
    }

    void prepareMaterials(LiveActor* pActor) {
        pActor->mModelManager->prepareMaterials();
    }

    void initMaterialFog(LiveActor* pActor, bool enable) {
        pActor->mModelManager->mMaterialUpdater->addFogCtrl(enable);
    }

    void initMaterialColor0(LiveActor* pActor, const char* pMatName, const Color8* pColor) {
        pActor->mModelManager->mMaterialUpdater->addMatColorCtrl(pMatName, 0, pColor);
    }

    TexMtxCtrl* initMaterialTexMtx(LiveActor* pActor, const char* pName) {
        return pActor->mModelManager->mMaterialUpdater->addTexMtxCtrl(pName);
    }

    ProjmapEffectMtxSetter* initMaterialProjmapEffectMtxSetter(LiveActor* pActor) {
        return pActor->mModelManager->mMaterialUpdater->addProjmapEffectMtxSetter();
    }

    void startAction(const LiveActor* pActor, const char* pName) {
        if (pActor->mAnimKeeper != nullptr && pActor->mAnimKeeper->start(pName)) {
            return;
        }

        tryStartAllAnim(pActor, pName);
    }

    bool isActionEnd(const LiveActor* pActor) {
        return pActor->mModelManager->isBckStopped();
    }

    bool isActionStart(const LiveActor* pActor, const char* pName) {
        if (pActor->mAnimKeeper != nullptr) {
            return pActor->mAnimKeeper->isPlaying(pName);
        }

        return isBckPlaying(pActor->mModelManager->mXanimePlayer, pName);
    }

    bool tryStartAction(const LiveActor* pActor, const char* pName) {
        if (!isActionStart(pActor, pName)) {
            startAction(pActor, pName);
            return true;
        }

        return false;
    }

    void startAllAnim(const LiveActor* pActor, const char* pName) {
        tryStartAllAnim(pActor, pName);
    }

    bool tryStartAllAnim(const LiveActor* pActor, const char* pName) {
        bool started = false;

        if (startBckIfExist(pActor, pName)) {
            started = true;
        }

        if (startBtkIfExist(pActor, pName)) {
            started = true;
        }

        if (startBrkIfExist(pActor, pName)) {
            started = true;
        }

        if (startBpkIfExist(pActor, pName)) {
            started = true;
        }

        if (startBtpIfExist(pActor, pName)) {
            started = true;
        }

        if (startBvaIfExist(pActor, pName)) {
            started = true;
        }

        return started;
    }

    void startBck(const LiveActor* pActor, const char* pBckName, const char* pBrkName) {
        pActor->mModelManager->startBck(pBckName, pBrkName);
        ::changeBckForEffectKeeper(pActor);
        startBas(pActor, pBckName, false, 0.0f, 0.0f);
    }

    void startBckWithInterpole(const LiveActor* pActor, const char* pBckName, s32 interpole) {
        pActor->mModelManager->startBckWithInterpole(pBckName, interpole);
        startBas(pActor, pBckName, false, 0.0f, 0.0f);
        ::changeBckForEffectKeeper(pActor);
    }

    void startBckNoInterpole(const LiveActor* pActor, const char* pBckName) {
        pActor->mModelManager->startBckWithInterpole(pBckName, 0);
        startBas(pActor, pBckName, false, 0.0f, 0.0f);
        ::changeBckForEffectKeeper(pActor);
    }

    void startBckAtFirstStep(const LiveActor* pActor, const char* pBckName) {
        if (!isStep(pActor, 0)) {
            return;
        }

        pActor->mModelManager->startBck(pBckName, nullptr);
        ::changeBckForEffectKeeper(pActor);
        startBas(pActor, pBckName, false, 0.0f, 0.0f);
    }

    bool tryStartBck(const LiveActor* pActor, const char* pBckName, const char* pBrkName) {
        if (!isBckPlaying(pActor->mModelManager->mXanimePlayer, pBckName)) {
            pActor->mModelManager->startBck(pBckName, pBrkName);
            ::changeBckForEffectKeeper(pActor);
            startBas(pActor, pBckName, false, 0.0f, 0.0f);
            return true;
        }

        return false;
    }

    bool tryStartBckAndBtp(const LiveActor* pActor, const char* pBckName, const char* pBtpName) {
        if (tryStartBck(pActor, pBckName, pBtpName)) {
            if (isExistBtp(pActor, pBckName)) {
                pActor->mModelManager->startBtp(pBckName);
            }

            return true;
        }

        return false;
    }

    void setAllAnimFrame(const LiveActor* pActor, const char* pName, f32 frame) {
        if (isExistBck(pActor, pName)) {
            getBckCtrl(pActor)->setFrame(frame);
        }

        if (isExistBtk(pActor, pName)) {
            getBtkCtrl(pActor)->setFrame(frame);
        }

        if (isExistBpk(pActor, pName)) {
            getBpkCtrl(pActor)->setFrame(frame);
        }

        if (isExistBtp(pActor, pName)) {
            getBtpCtrl(pActor)->setFrame(frame);
        }

        if (isExistBrk(pActor, pName)) {
            getBrkCtrl(pActor)->setFrame(frame);
        }

        if (isExistBva(pActor, pName)) {
            getBvaCtrl(pActor)->setFrame(frame);
        }
    }

    void setAllAnimFrameAndStop(const LiveActor* pActor, const char* pName, f32 frame) {
        if (isExistBck(pActor, pName)) {
            setBckFrameAndStop(pActor, frame);
        }

        if (isExistBtk(pActor, pName)) {
            setBtkFrameAndStop(pActor, frame);
        }

        if (isExistBpk(pActor, pName)) {
            setBpkFrameAndStop(pActor, frame);
        }

        if (isExistBtp(pActor, pName)) {
            setBtpFrameAndStop(pActor, frame);
        }

        if (isExistBrk(pActor, pName)) {
            setBrkFrameAndStop(pActor, frame);
        }

        if (isExistBva(pActor, pName)) {
            setBvaFrameAndStop(pActor, frame);
        }
    }

    void setAllAnimFrameAtEnd(const LiveActor* pActor, const char* pName) {
        if (isExistBck(pActor, pName)) {
            setBckFrame(pActor, getBckCtrl(pActor)->getEnd());
        }

        if (isExistBtk(pActor, pName)) {
            setBtkFrame(pActor, getBtkCtrl(pActor)->getEnd());
        }

        if (isExistBpk(pActor, pName)) {
            setBpkFrame(pActor, getBpkCtrl(pActor)->getEnd());
        }

        if (isExistBtp(pActor, pName)) {
            setBtpFrame(pActor, getBtpCtrl(pActor)->getEnd());
        }

        if (isExistBrk(pActor, pName)) {
            setBrkFrame(pActor, getBrkCtrl(pActor)->getEnd());
        }

        // The visibility animation's own frame (setting the colour animation's here would leave every
        // shape a BVA shows only at its end hidden).
        if (isExistBva(pActor, pName)) {
            setBvaFrame(pActor, getBvaCtrl(pActor)->getEnd());
        }
    }

    bool isAnyAnimStopped(const LiveActor* pActor, const char* pName) {
        if (isExistBck(pActor, pName) && isBckStopped(pActor)) {
            return true;
        }

        if (isExistBtk(pActor, pName) && isBtkStopped(pActor)) {
            return true;
        }

        if (isExistBpk(pActor, pName) && isBpkStopped(pActor)) {
            return true;
        }

        if (isExistBtp(pActor, pName) && isBtpStopped(pActor)) {
            return true;
        }

        if (isExistBrk(pActor, pName) && isBrkStopped(pActor)) {
            return true;
        }

        if (isExistBva(pActor, pName) && isBvaStopped(pActor)) {
            return true;
        }

        return false;
    }

    bool isAnyAnimOneTimeAndStopped(const LiveActor* pActor, const char* pName) {
        if (isExistBck(pActor, pName) && isBckStopped(pActor)) {
            return true;
        }

        if (isExistBtk(pActor, pName) && isBtkStopped(pActor)) {
            return true;
        }

        if (isExistBpk(pActor, pName) && isBpkStopped(pActor)) {
            return true;
        }

        if (isExistBtp(pActor, pName) && isBtpStopped(pActor)) {
            return true;
        }

        if (isExistBrk(pActor, pName) && isBrkStopped(pActor)) {
            return true;
        }

        if (isExistBva(pActor, pName) && isBvaStopped(pActor)) {
            return true;
        }

        return false;
    }

    const char* getPlayingBckName(const LiveActor* pActor) {
        return pActor->mModelManager->getPlayingBckName();
    }

    void setBinderIgnoreMovingCollision(LiveActor* pActor) {
        pActor->mBinder->mIsMoveWithCollision = false;
    }

    LiveActorGroup* joinToGroup(LiveActor* pActor, const char* pGroupName) {
        LiveActorGroup* pGroup = static_cast< LiveActorGroup* >(NameObjFinder::find(pGroupName));
        pGroup->registerActor(pActor);
        return pGroup;
    }

    MsgSharedGroup* joinToGroupArray(LiveActor* pActor, const TableIter& rIter, const char* pName, s32 maxCount) {
        if (!rIter.isValid()) {
            return nullptr;
        }

        s32 groupId;

        if (getPlacementGroupID(rIter, &groupId)) {
            LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
            return (MsgSharedGroup*)pGroupArray->entry(pActor, rIter, pName, maxCount);
        }

        return nullptr;
    }

    LiveActorGroup* getGroupFromArray(const LiveActor* pActor) {
        LiveActorGroupArray* pGroupArray = MR::getSceneObj< LiveActorGroupArray >(SceneObj_LiveActorGroupArray);
        return pGroupArray->getLiveActorGroup(pActor);
    }

    LiveActor* getPairedGroupMember(const LiveActor* pActor) {
        // Placed with no group, or in a level with none: no partner.
        if (!isExistSceneObj(SceneObj_LiveActorGroupArray)) {
            return nullptr;
        }

        LiveActorGroup* pGroup = getGroupFromArray(pActor);
        if (pGroup == nullptr) {
            return nullptr;
        }

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            if (pGroup->getActor(i) != pActor) {
                return pGroup->getActor(i);
            }
        }

        return nullptr;
    }

    void callMakeActorDeadAllGroupMember(const LiveActor* pActor) {
        ::callMethodAllGroupMember(pActor, &LiveActor::makeActorDead);
    }

    void callKillAllGroupMember(const LiveActor* pActor) {
        ::callMethodAllGroupMember(pActor, &LiveActor::kill);
    }

    void callMakeActorAppearedAllGroupMember(const LiveActor* pActor) {
        ::callMethodAllGroupMember(pActor, &LiveActor::makeActorAppeared);
    }

    void callAppearAllGroupMember(const LiveActor* pActor) {
        ::callMethodAllGroupMember(pActor, &LiveActor::appear);
    }

    void callRequestMovementOnAllGroupMember(const LiveActor* pActor) {
        ::callFuncAllGroupMember(static_cast<const NameObj*>(pActor), requestMovementOn);
    }

    void callInvalidateClippingAllGroupMember(const LiveActor* pActor) {
        ::callFuncAllGroupMember(pActor, invalidateClipping);
    }

    void callValidateClippingAllGroupMember(const LiveActor* pActor) {
        ::callFuncAllGroupMember(pActor, validateClipping);
    }

    s32 countHideGroupMember(const LiveActor* pActor) {
        return countGroupMember(pActor, isHiddenModel);
    }

    s32 countShowGroupMember(const LiveActor* pActor) {
        return countGroupMember(pActor, ::isShowModel);
    }

    void addToAttributeGroupSearchTurtle(const LiveActor* pActor) {
        GroupCheckManager* pGroup = MR::getSceneObj< GroupCheckManager >(SceneObj_GroupCheckManager);
        pGroup->add(pActor, 0);
    }

    void addToAttributeGroupReflectSpinningBox(const LiveActor* pActor) {
        GroupCheckManager* pGroup = MR::getSceneObj< GroupCheckManager >(SceneObj_GroupCheckManager);
        pGroup->add(pActor, 1);
    }

    bool isExistInAttributeGroupSearchTurtle(const LiveActor* pActor) {
        GroupCheckManager* pGroup = MR::getSceneObj< GroupCheckManager >(SceneObj_GroupCheckManager);
        return pGroup->isExist(pActor, 0);
    }

    bool isExistInAttributeGroupReflectSpinningBox(const LiveActor* pActor) {
        GroupCheckManager* pGroup = MR::getSceneObj< GroupCheckManager >(SceneObj_GroupCheckManager);
        return pGroup->isExist(pActor, 1);
    }

    bool isBckStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBckStopped();
    }

    bool isBtkStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBtkStopped();
    }

    bool isBrkStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBrkStopped();
    }

    bool isBtpStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBtpStopped();
    }

    bool isBpkStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBpkStopped();
    }

    bool isBvaStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBvaStopped();
    }

    bool isBckOneTimeAndStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBckStopped();
    }

    bool isBrkOneTimeAndStopped(const LiveActor* pActor) {
        return pActor->mModelManager->isBrkStopped();
    }

    bool isBckLooped(const LiveActor* pActor) {
        return (getBckCtrl(pActor)->mState & 0x2) != 0;
    }

    bool checkPassBckFrame(const LiveActor* pActor, f32 f) {
        return getBckCtrl(pActor)->checkPass(f) == 1;
    }

    void setBckFrameAtRandom(const LiveActor* pActor) {
        s32 randomFrame = getBckCtrl(pActor)->getEnd() * MR::getRandom();

        setBckFrame(pActor, randomFrame);
    }

    void setBtkFrameAtRandom(const LiveActor* pActor) {
        s32 randomFrame = getBtkCtrl(pActor)->getEnd() * MR::getRandom();

        pActor->mModelManager->getBtkCtrl()->mFrame = randomFrame;
    }

    void setBckFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBckCtrl(pActor)->setFrame(frame);
        getBckCtrl(pActor)->setRate(0.0f);
    }

    void setBtkFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBtkCtrl(pActor)->setFrame(frame);
        getBtkCtrl(pActor)->setRate(0.0f);
    }

    void setBrkFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBrkCtrl(pActor)->setFrame(frame);
        getBrkCtrl(pActor)->setRate(0.0f);
    }

    void setBtpFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBtpCtrl(pActor)->setFrame(frame);
        getBtpCtrl(pActor)->setRate(0.0f);
    }

    void setBpkFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBpkCtrl(pActor)->setFrame(frame);
        getBpkCtrl(pActor)->setRate(0.0f);
    }

    void setBvaFrameAndStop(const LiveActor* pActor, f32 frame) {
        getBvaCtrl(pActor)->setFrame(frame);
        getBvaCtrl(pActor)->setRate(0.0f);
    }

    void setBrkFrameEndAndStop(const LiveActor* pActor) {
        getBrkCtrl(pActor)->setFrame(getBrkCtrl(pActor)->getEnd());
        getBrkCtrl(pActor)->setRate(0.0f);
    }

    void startBtkAndSetFrameAndStop(const LiveActor* pActor, const char* pBtkName, f32 frame) {
        startBtk(pActor, pBtkName);
        setBtkFrameAndStop(pActor, frame);
    }

    void startBrkAndSetFrameAndStop(const LiveActor* pActor, const char* pBrkName, f32 frame) {
        startBrk(pActor, pBrkName);
        setBrkFrameAndStop(pActor, frame);
    }

    void startBtpAndSetFrameAndStop(const LiveActor* pActor, const char* pBtpName, f32 frame) {
        startBtp(pActor, pBtpName);
        setBtpFrameAndStop(pActor, frame);
    }

    void startBtk(const LiveActor* pActor, const char* pBtkName) {
        pActor->mModelManager->startBtk(pBtkName);
    }

    void startBrk(const LiveActor* pActor, const char* pBrkName) {
        pActor->mModelManager->startBrk(pBrkName);
    }

    void startBtp(const LiveActor* pActor, const char* pBtpName) {
        pActor->mModelManager->startBtp(pBtpName);
    }

    void startBpk(const LiveActor* pActor, const char* pBpkName) {
        pActor->mModelManager->startBpk(pBpkName);
    }

    void startBva(const LiveActor* pActor, const char* pBvaName) {
        pActor->mModelManager->startBva(pBvaName);
    }

    bool startBckIfExist(const LiveActor* pActor, const char* pBckName) {
        if (getResourceHolder(pActor)->mMotionResTable->isExistRes(pBckName)) {
            pActor->mModelManager->startBck(pBckName, nullptr);
            ::changeBckForEffectKeeper(pActor);
            startBas(pActor, pBckName, false, 0.0f, 0.0f);
            return true;
        }

        return false;
    }

    bool startBtkIfExist(const LiveActor* pActor, const char* pBtkName) {
        if (getResourceHolder(pActor)->mBtkResTable->isExistRes(pBtkName)) {
            pActor->mModelManager->startBtk(pBtkName);
            return true;
        }

        return false;
    }

    bool startBrkIfExist(const LiveActor* pActor, const char* pBrkName) {
        if (getResourceHolder(pActor)->mBrkResTable->isExistRes(pBrkName)) {
            pActor->mModelManager->startBrk(pBrkName);
            return true;
        }

        return false;
    }

    bool startBtpIfExist(const LiveActor* pActor, const char* pBtpName) {
        if (getResourceHolder(pActor)->mBtpResTable->isExistRes(pBtpName)) {
            pActor->mModelManager->startBtp(pBtpName);
            return true;
        }

        return false;
    }

    bool startBpkIfExist(const LiveActor* pActor, const char* pBpkName) {
        if (getResourceHolder(pActor)->mBpkResTable->isExistRes(pBpkName)) {
            pActor->mModelManager->startBpk(pBpkName);
            return true;
        }

        return false;
    }

    bool startBvaIfExist(const LiveActor* pActor, const char* pBvaName) {
        if (getResourceHolder(pActor)->mBvaResTable->isExistRes(pBvaName)) {
            pActor->mModelManager->startBva(pBvaName);
            return true;
        }

        return false;
    }

    bool isBtkPlaying(const LiveActor* pActor, const char* pBtkName) {
        return pActor->mModelManager->isBtkPlaying(pBtkName);
    }

    bool isBrkPlaying(const LiveActor* pActor, const char* pBrkName) {
        return pActor->mModelManager->isBrkPlaying(pBrkName);
    }

    bool isBtpPlaying(const LiveActor* pActor, const char* pBtpName) {
        return pActor->mModelManager->isBtpPlaying(pBtpName);
    }

    bool isBpkPlaying(const LiveActor* pActor, const char* pBpkName) {
        return pActor->mModelManager->isBpkPlaying(pBpkName);
    }

    bool isBvaPlaying(const LiveActor* pActor, const char* pBvaName) {
        return pActor->mModelManager->isBvaPlaying(pBvaName);
    }

    bool isBckExist(const LiveActor* pActor, const char* pBckName) {
        ResTable* pBckResTable = getResourceHolder(pActor)->mMotionResTable;

        if (pBckResTable->mCount != 0) {
            if (pBckResTable->isExistRes(pBckName)) {
                return true;
            }
        }

        return false;
    }

    bool isBtkExist(const LiveActor* pActor, const char* pBtkName) {
        ResTable* pBtkResTable = getResourceHolder(pActor)->mBtkResTable;

        if (pBtkResTable->mCount != 0) {
            if (pBtkResTable->isExistRes(pBtkName)) {
                return true;
            }
        }

        return false;
    }

    bool isBrkExist(const LiveActor* pActor, const char* pBrkName) {
        ResTable* pBrkResTable = getResourceHolder(pActor)->mBrkResTable;

        if (pBrkResTable->mCount != 0) {
            if (pBrkResTable->isExistRes(pBrkName)) {
                return true;
            }
        }

        return false;
    }

    bool isBpkExist(const LiveActor* pActor, const char* pBpkName) {
        ResTable* pBpkResTable = getResourceHolder(pActor)->mBpkResTable;

        if (pBpkResTable->mCount != 0) {
            if (pBpkResTable->isExistRes(pBpkName)) {
                return true;
            }
        }

        return false;
    }

    bool isBtpExist(const LiveActor* pActor, const char* pBtpName) {
        ResTable* pBtpResTable = getResourceHolder(pActor)->mBtpResTable;

        if (pBtpResTable->mCount != 0) {
            if (pBtpResTable->isExistRes(pBtpName)) {
                return true;
            }
        }

        return false;
    }

    bool isBvaExist(const LiveActor* pActor, const char* pBvaName) {
        ResTable* pBvaResTable = getResourceHolder(pActor)->mBvaResTable;

        if (pBvaResTable->mCount != 0) {
            if (pBvaResTable->isExistRes(pBvaName)) {
                return true;
            }
        }

        return false;
    }

    void stopBck(const LiveActor* pActor) {
        getBckCtrl(pActor)->setRate(0.0f);
    }

    void stopBtk(const LiveActor* pActor) {
        pActor->mModelManager->stopBtk();
    }

    void stopBrk(const LiveActor* pActor) {
        pActor->mModelManager->stopBrk();
    }

    void stopBtp(const LiveActor* pActor) {
        pActor->mModelManager->stopBtp();
    }

    void stopBva(const LiveActor* pActor) {
        pActor->mModelManager->stopBva();
    }

    void setBckRate(const LiveActor* pActor, f32 rate) {
        FrameCtrl* pCtrl = getBckCtrl(pActor);
        f32 scale = 1.0f;
        pCtrl->setRate(rate * scale);
    }

    void setBtkRate(const LiveActor* pActor, f32 rate) {
        FrameCtrl* pCtrl = getBtkCtrl(pActor);
        f32 scale = 1.0f;
        pCtrl->setRate(rate * scale);
    }

    void setBrkRate(const LiveActor* pActor, f32 rate) {
        FrameCtrl* pCtrl = getBrkCtrl(pActor);
        f32 scale = 1.0f;
        pCtrl->setRate(rate * scale);
    }

    void setBvaRate(const LiveActor* pActor, f32 rate) {
        FrameCtrl* pCtrl = getBvaCtrl(pActor);
        f32 scale = 1.0f;
        pCtrl->setRate(rate * scale);
    }

    void setBckFrame(const LiveActor* pActor, f32 frame) {
        getBckCtrl(pActor)->setFrame(frame);
    }

    void setBtkFrame(const LiveActor* pActor, f32 frame) {
        getBtkCtrl(pActor)->setFrame(frame);
    }

    void setBrkFrame(const LiveActor* pActor, f32 frame) {
        getBrkCtrl(pActor)->setFrame(frame);
    }

    void setBtpFrame(const LiveActor* pActor, f32 frame) {
        getBtpCtrl(pActor)->setFrame(frame);
    }

    void setBpkFrame(const LiveActor* pActor, f32 frame) {
        getBpkCtrl(pActor)->setFrame(frame);
    }

    void setBvaFrame(const LiveActor* pActor, f32 frame) {
        getBvaCtrl(pActor)->setFrame(frame);
    }

    bool isBckPlaying(const LiveActor* pActor, const char* pBckName) {
        return MR::isBckPlaying(pActor->mModelManager->mXanimePlayer, pBckName);
    }

    FrameCtrl* getBckCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBckCtrl();
    }

    FrameCtrl* getBtkCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBtkCtrl();
    }

    FrameCtrl* getBrkCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBrkCtrl();
    }

    FrameCtrl* getBtpCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBtpCtrl();
    }

    FrameCtrl* getBpkCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBpkCtrl();
    }

    FrameCtrl* getBvaCtrl(const LiveActor* pActor) {
        return pActor->mModelManager->getBvaCtrl();
    }

    void updateMaterial(LiveActor* pActor) {
        pActor->mModelManager->updateList();
    }

    void setMirrorReflectionInfoFromMtxYUp(const Pos3f& rMtx) {
        Vec3f up;
        Vec3f pos;
        rMtx.getYDir(up);
        rMtx.getTrans(pos);
        MR::getMirrorCamera()->setMirrorMapInfo(up, pos);
    }

    void setMirrorReflectionInfoFromModel(LiveActor* pActor) {
        ModelData* pModelData = MR::getModelData(pActor);
        MR::getMirrorCamera()->setMirrorMapInfo(pModelData);
    }

    void changeModelDataTexAll(LiveActor* pActor, const char* pTexName, const ResTIMG& rTimg) {
        ModelData* pModelData = MR::getModelData(pActor);

        for (u32 texIndex = 0; texIndex < pModelData->getTextureCount(); texIndex++) {
            const char* name = pModelData->getTextureName(texIndex);

            if (!MR::isEqualString(name, pTexName)) {
                continue;
            }

            pModelData->setTexture(texIndex, rTimg);
        }
    }

    void initJointTransform(const LiveActor* pActor) {
        pActor->mModelManager->initJointTransform();
    }

    XjointTransform* getJointTransform(const LiveActor* pActor, const char* pName) {
        return pActor->mModelManager->getJointTransform(pName);
    }

    void setJointTransformLocalMtx(const LiveActor* pActor, const char* pName, MtxPtr pMtx) {
        XjointTransform* pTransform = pActor->mModelManager->getJointTransform(pName);
        pTransform->mLocalMtx = pMtx;
    }

    f32 getBckFrame(const LiveActor* pActor) {
        return getBckCtrl(pActor)->getFrame();
    }

    f32 getBrkFrame(const LiveActor* pActor) {
        return getBrkCtrl(pActor)->getFrame();
    }

    f32 getBtpFrame(const LiveActor* pActor) {
        return getBtpCtrl(pActor)->getFrame();
    }

    f32 getBvaFrame(const LiveActor* pActor) {
        return getBvaCtrl(pActor)->getFrame();
    }

    f32 getBckRate(const LiveActor* pActor) {
        return getBckCtrl(pActor)->getRate();
    }

    f32 getBckFrameMax(const LiveActor* pActor) {
        return getBckCtrl(pActor)->getEnd();
    }

    f32 getBtkFrameMax(const LiveActor* pActor) {
        return getBtkCtrl(pActor)->getEnd();
    }

    f32 getBrkFrameMax(const LiveActor* pActor) {
        return getBrkCtrl(pActor)->getEnd();
    }

    void reflectBckCtrlData(LiveActor* pActor, const BckCtrlData& rBck) {
        BckCtrlFunction::reflectBckCtrlData(rBck, pActor->mModelManager->mXanimePlayer);

        AudAnmSoundObject* pSoundObj = pActor->mSoundObject;

        if (pSoundObj != nullptr && pSoundObj->hasAnim()) {
            if (rBck.mRepeatFrame < 0) {
                pSoundObj->setLoopFrame((f32)rBck.mStartFrame, (f32)rBck.mEndFrame);
            } else {
                pSoundObj->setLoopFrame((f32)rBck.mRepeatFrame, (f32)rBck.mEndFrame);
            }

            pSoundObj->setStartPos((f32)rBck.mStartFrame);
        }
    }

    void initLightCtrl(LiveActor* pActor) {
        pActor->initActorLightCtrl();
        pActor->mActorLightCtrl->init(-1, false);
    }

    void initLightCtrlForPlayer(LiveActor* pActor) {
        pActor->initActorLightCtrl();
        pActor->mActorLightCtrl->init(-1, false);
        LightFunction::registerPlayerLightCtrl(pActor->mActorLightCtrl);
    }

    void initLightCtrlNoDrawEnemy(LiveActor* pActor) {
        pActor->initActorLightCtrl();
        pActor->mActorLightCtrl->init(1, true);
    }

    void initLightCtrlNoDrawMapObj(LiveActor* pActor) {
        pActor->initActorLightCtrl();
        pActor->mActorLightCtrl->init(3, true);
    }

    void updateLightCtrl(LiveActor* pActor) {
        pActor->mActorLightCtrl->update(false);
    }

    void updateLightCtrlDirect(LiveActor* pActor) {
        pActor->mActorLightCtrl->update(true);
    }

    void loadActorLight(const LiveActor* pActor) {
        pActor->mActorLightCtrl->loadLight();
    }

    void calcLightPos0(Vec3f* pPos, const LiveActor* pActor) {
        LightFunction::calcLightWorldPos(pPos, pActor->mActorLightCtrl->getActorLight()->mInfo0);
    }

    void calcLightPos1(Vec3f* pPos, const LiveActor* pActor) {
        LightFunction::calcLightWorldPos(pPos, pActor->mActorLightCtrl->getActorLight()->mInfo1);
    }

    const NebulaColor8* getLightAmbientColor(const LiveActor* pActor) {
        return &pActor->mActorLightCtrl->getActorLight()->mColor;
    }

    ActorLightCtrl* getLightCtrl(const LiveActor* pActor) {
        return pActor->mActorLightCtrl;
    }

    bool isStep(const LiveActor* pActor, s32 step) {
        return pActor->getNerveStep() == step;
    }

    bool isFirstStep(const LiveActor* pActor) {
        return isStep(pActor, 0);
    }

    bool isLessStep(const LiveActor* pActor, s32 step) {
        return pActor->getNerveStep() < step;
    }

    bool isLessEqualStep(const LiveActor* pActor, s32 step) {
        return pActor->getNerveStep() <= step;
    }

    bool isGreaterStep(const LiveActor* pActor, s32 step) {
        return pActor->getNerveStep() > step;
    }

    bool isGreaterEqualStep(const LiveActor* pActor, s32 step) {
        return pActor->getNerveStep() >= step;
    }

    bool isIntervalStep(const LiveActor* pActor, s32 step) {
        return (step != 0 ? pActor->getNerveStep() % step : pActor->getNerveStep()) == 0;
    }

    bool isNewNerve(const LiveActor* pActor) {
        return pActor->getNerveStep() < 0;
    }

    f32 calcNerveRate(const LiveActor* pActor, s32 stepMax) {
        return stepMax <= 0 ? 1.0f : clamp(static_cast< f32 >(pActor->getNerveStep()) / stepMax, 0.0f, 1.0f);
    }

    f32 calcNerveRate(const LiveActor* pActor, s32 stepMin, s32 stepMax) {
        return clamp(normalize(pActor->getNerveStep(), stepMin, stepMax), 0.0f, 1.0f);
    }

    f32 calcNerveEaseInRate(const LiveActor* pActor, s32 stepMax) {
        return getEaseInValue(calcNerveRate(pActor, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveEaseOutRate(const LiveActor* pActor, s32 stepMax) {
        return getEaseOutValue(calcNerveRate(pActor, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveEaseOutRate(const LiveActor* pActor, s32 stepMin, s32 stepMax) {
        return getEaseOutValue(calcNerveRate(pActor, stepMin, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveEaseInOutRate(const LiveActor* pActor, s32 stepMax) {
        return getEaseInOutValue(calcNerveRate(pActor, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveEaseInOutRate(const LiveActor* pActor, s32 stepMin, s32 stepMax) {
        return getEaseInOutValue(calcNerveRate(pActor, stepMin, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveValue(const LiveActor* pActor, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getLinerValue(calcNerveRate(pActor, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveValue(const LiveActor* pActor, s32 stepMin, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getLinerValue(calcNerveRate(pActor, stepMin, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveEaseInValue(const LiveActor* pActor, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getEaseInValue(calcNerveRate(pActor, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveEaseInValue(const LiveActor* pActor, s32 stepMin, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getEaseInValue(calcNerveRate(pActor, stepMin, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveEaseOutValue(const LiveActor* pActor, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getEaseOutValue(calcNerveRate(pActor, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveEaseInOutValue(const LiveActor* pActor, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getEaseInOutValue(calcNerveRate(pActor, stepMax), valueStart, valueEnd, 1.0f);
    }

    f32 calcNerveEaseInOutValue(const LiveActor* pActor, s32 stepMin, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return getEaseInOutValue(calcNerveRate(pActor, stepMin, stepMax), valueStart, valueEnd, 1.0f);
    }

    void setNerveAtStep(LiveActor* pActor, const Nerve* pNerve, s32 step) {
        if (pActor->getNerveStep() == step) {
            pActor->setNerve(pNerve);
        }
    }

    void setNerveAtBckStopped(LiveActor* pActor, const Nerve* pNerve) {
        if (pActor->mModelManager->isBckStopped()) {
            pActor->setNerve(pNerve);
        }
    }

    bool trySetNerve(LiveActor* pActor, const Nerve* pNerve) {
        if (pActor->isNerve(pNerve)) {
            return false;
        }

        pActor->setNerve(pNerve);

        return true;
    }

    const Vec3f* getGroundNormal(const LiveActor* pActor) {
        return pActor->mBinder->mGroundInfo.mParentTriangle.getNormal(0);
    }

    const Vec3f* getWallNormal(const LiveActor* pActor) {
        return pActor->mBinder->mWallInfo.mParentTriangle.getNormal(0);
    }

    const Vec3f* getRoofNormal(const LiveActor* pActor) {
        return pActor->mBinder->mRoofInfo.mParentTriangle.getNormal(0);
    }

    const Vec3f* getBindedNormal(const LiveActor* pActor) {
        if (isBindedGround(pActor)) {
            return getGroundNormal(pActor);
        }

        if (isBindedWall(pActor)) {
            return getWallNormal(pActor);
        }

        if (isBindedRoof(pActor)) {
            return getRoofNormal(pActor);
        }

        return getGroundNormal(pActor);
    }

    const Vec3f* getBindedHitPos(const LiveActor* pActor) {
        if (isBindedGround(pActor)) {
            return getGroundHitPos(pActor);
        }

        if (isBindedWall(pActor)) {
            return getWallHitPos(pActor);
        }

        if (isBindedRoof(pActor)) {
            return getRoofHitPos(pActor);
        }

        return getGroundHitPos(pActor);
    }

    const Vec3f* getGroundHitPos(const LiveActor* pActor) {
        return &pActor->mBinder->mGroundInfo.mHitPos;
    }

    const Vec3f* getWallHitPos(const LiveActor* pActor) {
        return &pActor->mBinder->mWallInfo.mHitPos;
    }

    const Vec3f* getRoofHitPos(const LiveActor* pActor) {
        return &pActor->mBinder->mRoofInfo.mHitPos;
    }

    void calcWallNormalHorizontal(Vec3f* pVec, const LiveActor* pActor) {
        const Vec3f* normal = getWallNormal(pActor);
        const Vec3f* grav = &pActor->mGravity;
        pVec->killElement(*normal, *grav);
    }

    f32 calcHitPowerToGround(const LiveActor* pActor) {
        if (!isBindedGround(pActor)) {
            return 0.0f;
        }

        f32 dot = pActor->mVelocity.dot(*getGroundNormal(pActor));

        if (-dot > 0.0f) {
            return -dot;
        }

        return 0.0f;
    }

    f32 calcHitPowerToWall(const LiveActor* pActor) {
        if (!isBindedWall(pActor)) {
            return 0.0f;
        }

        f32 dot = pActor->mVelocity.dot(*getWallNormal(pActor));

        if (-dot > 0.0f) {
            return -dot;
        }

        return 0.0f;
    }

    int getBindedPlaneNum(const LiveActor* pActor) {
        return pActor->mBinder->mPlaneNum;
    }

    const Vec3f* getBindedPlaneNormal(const LiveActor* pActor, int planeIndex) {
        return pActor->mBinder->getPlane(planeIndex)->mParentTriangle.getNormal(0);
    }

    HitSensor* getBindedPlaneSensor(const LiveActor* pActor, int planeIndex) {
        return pActor->mBinder->getPlane(planeIndex)->mParentTriangle.mSensor;
    }

    Vec3f* getBindedFixReactionVector(const LiveActor* pActor) {
        return &pActor->mBinder->mFixReactionVector;
    }

    void setBinderOffsetVec(LiveActor* pActor, const Vec3f* pVec, bool isInHostFrame) {
        Binder* binder = pActor->mBinder;
        binder->mOffsetVec = pVec;
        binder->mIsOffsetInHostFrame = isInHostFrame;
    }

    void setBinderRadius(LiveActor* pActor, f32 radius) {
        pActor->mBinder->mRadius = radius;
    }

    f32 getBinderRadius(const LiveActor* pActor) {
        return pActor->mBinder->mRadius;
    }

    void setBinderCollisionPartsFilter(LiveActor* pActor, CollisionPartsFilterBase* pFilter) {
        pActor->mBinder->setCollisionPartsFilter(pFilter);
    }

    void setBinderExceptActor(LiveActor* pActor, const LiveActor* pExcept) {
        CollisionPartsFilterActor* pFilter = new CollisionPartsFilterActor(pExcept);
        pActor->mBinder->setCollisionPartsFilter(pFilter);
    }

    void setBindTriangleFilter(LiveActor* pActor, TriangleFilterBase* pTriangleFilter) {
        pActor->mBinder->setTriangleFilter(pTriangleFilter);
    }

    bool isExistBinder(const LiveActor* pActor) {
        return pActor->mBinder != nullptr;
    }

    void onEntryDrawBuffer(LiveActor* pActor) {
        if (!isNoEntryDrawBuffer(pActor)) {
            return;
        }

        if (!isDead(pActor) && !pActor->mFlag.mIsClipped) {
            connectToDrawTemporarily(pActor);
        }

        pActor->mFlag.mIsHiddenModel = false;
    }

    void offEntryDrawBuffer(LiveActor* pActor) {
        if (isNoEntryDrawBuffer(pActor)) {
            return;
        }

        if (!isDead(pActor) && (!pActor->mFlag.mIsClipped || pActor->mFlag.mIsDrawnClipped)) {
            disconnectToDrawTemporarily(pActor);
        }

        pActor->mFlag.mIsDrawnClipped = false;
        pActor->mFlag.mIsHiddenModel = true;
    }

    bool isDead(const LiveActor* pActor) {
        return pActor->mFlag.mIsDead;
    }

    bool isHiddenModel(const LiveActor* pActor) {
        return pActor->mFlag.mIsHiddenModel;
    }

    void showModel(LiveActor* pActor) {
        if (isNoCalcAnim(pActor)) {
            onCalcAnim(pActor);
        }

        if (isNoCalcView(pActor)) {
            pActor->mFlag.mIsNoCalcView = false;
        }

        if (isNoEntryDrawBuffer(pActor)) {
            onEntryDrawBuffer(pActor);
        }
    }

    void hideModel(LiveActor* pActor) {
        if (!isNoCalcAnim(pActor)) {
            offCalcAnim(pActor);
        }

        if (!isNoCalcView(pActor)) {
            pActor->mFlag.mIsNoCalcView = true;
        }

        if (!isNoEntryDrawBuffer(pActor)) {
            offEntryDrawBuffer(pActor);
        }
    }

    void hideModelAndOnCalcAnim(LiveActor* pActor) {
        hideModel(pActor);
        onCalcAnim(pActor);
    }

    void showModelIfHidden(LiveActor* pActor) {
        if (isHiddenModel(pActor)) {
            showModel(pActor);
        }
    }

    void hideModelIfShown(LiveActor* pActor) {
        if (isHiddenModel(pActor)) {
            return;
        }

        hideModel(pActor);
    }

    void hideModelAndOnCalcAnimIfShown(LiveActor* pActor) {
        if (isHiddenModel(pActor)) {
            return;
        }

        hideModel(pActor);
        onCalcAnim(pActor);
    }

    void stopAnimFrame(LiveActor* pActor) {
        pActor->mFlag.mIsStoppedAnim = true;
    }

    void releaseAnimFrame(LiveActor* pActor) {
        pActor->mFlag.mIsStoppedAnim = false;
    }

    bool isNoCalcAnim(const LiveActor* pActor) {
        return pActor->mFlag.mIsNoCalcAnim;
    }

    void onCalcAnim(LiveActor* pActor) {
        pActor->mFlag.mIsNoCalcAnim = false;
    }

    void offCalcAnim(LiveActor* pActor) {
        pActor->mFlag.mIsNoCalcAnim = true;
    }

    bool isNoCalcView(const LiveActor* pActor) {
        return pActor->mFlag.mIsNoCalcView;
    }

    bool isNoEntryDrawBuffer(const LiveActor* pActor) {
        return pActor->mFlag.mIsHiddenModel;
    }

    bool isNoBind(const LiveActor* pActor) {
        return pActor->mFlag.mIsNoBind;
    }

    void onBind(LiveActor* pActor) {
        pActor->mFlag.mIsNoBind = false;
    }

    void offBind(LiveActor* pActor) {
        pActor->mFlag.mIsNoBind = true;
    }

    void calcGravity(LiveActor* pActor) {
        Vec3f gravity;
        calcGravityVector(pActor, pActor->mPosition, &gravity, nullptr, 0);

        if (!isNearZero(gravity)) {
            pActor->mGravity.set(gravity);
        }
    }

    void calcGravity(LiveActor* pActor, const Vec3f& rPos) {
        Vec3f gravity;
        calcGravityVector(pActor, rPos, &gravity, nullptr, 0);

        if (!isNearZero(gravity)) {
            pActor->mGravity.set(gravity);
        }
    }

    void calcGravityOrZero(LiveActor* pActor) {
        calcGravityOrZero(pActor, pActor->mPosition);
    }

    void calcGravityOrZero(LiveActor* pActor, const Vec3f& rPos) {
        Vec3f gravity;
        calcGravityVectorOrZero(pActor, rPos, &gravity, nullptr, 0);

        if (!isNearZero(gravity)) {
            pActor->mGravity.set(gravity);
            return;
        }

        if (isBindedGround(pActor)) {
            const Vec3f* normal = pActor->mBinder->mGroundInfo.mParentTriangle.getNormal(0);
            pActor->mGravity.set(-*normal);
        }
    }

    void initFur(LiveActor* pActor) {
        initMultiFur(pActor, -1);
    }

    void initFurPlanet(LiveActor* pActor) {
        initMultiFur(pActor, 3);
    }

    FurMulti* initFurPlayer(LiveActor* pActor) {
        return initMultiFur(pActor, 0);
    }

    void initCollisionParts(LiveActor* pActor, const char* pName, HitSensor* pSensor, MtxPtr pMtx) {
        pActor->initActorCollisionParts(pName, pSensor, nullptr, pMtx, false, false);
    }

    void initCollisionPartsAutoEqualScale(LiveActor* pActor, const char* pName, HitSensor* pSensor, MtxPtr pMtx) {
        pActor->initActorCollisionParts(pName, pSensor, nullptr, pMtx, true, false);
    }

    void initCollisionPartsAutoEqualScaleOne(LiveActor* pActor, const char* pName, HitSensor* pSensor, MtxPtr pMtx) {
        pActor->initActorCollisionParts(pName, pSensor, nullptr, pMtx, true, true);
    }

    void initCollisionPartsFromResourceHolder(LiveActor* pActor, const char* pName, HitSensor* pSensor, ResourceHolder* pResHolder, MtxPtr pMtx) {
        pActor->initActorCollisionParts(pName, pSensor, pResHolder, pMtx, false, false);
    }

    CollisionParts* createCollisionPartsFromLiveActor(LiveActor* pActor, const char* pName, HitSensor* pSensor, CollisionScaleType scaleType) {
        Pos3f mtx;
        makeMtxTRS(mtx, pActor);
        return ::createCollisionParts(getResourceHolder(pActor), pName, pSensor, mtx, scaleType, 0);
    }

    CollisionParts* createCollisionPartsFromLiveActor(LiveActor* pActor, const char* pName, HitSensor* pSensor, MtxPtr pMtx,
                                                      CollisionScaleType scaleType) {
        Pos3f mtx;
        mtx.set(pMtx);
        CollisionParts* pParts = ::createCollisionParts(getResourceHolder(pActor), pName, pSensor, mtx, scaleType, 0);
        pParts->mFollowMtx = pMtx;
        return pParts;
    }

    CollisionParts* createCollisionPartsFromResourceHolder(ResourceHolder* pResHolder, const char* pName, HitSensor* pSensor, const Pos3f& rMtx,
                                                           CollisionScaleType scaleType) {
        return ::createCollisionParts(pResHolder, pName, pSensor, rMtx, scaleType, 0);
    }

    CollisionParts* tryCreateCollisionMoveLimit(LiveActor* pActor, MtxPtr pMtx, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "MoveLimit";
            kcl.format("%s.kcl", pKclName);
        }

        if (!getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "MoveLimit";
        mtx.set(pMtx);
        CollisionParts* pParts = ::createCollisionParts(getResourceHolder(pActor), pCollisionName, pSensor, mtx, CollisionScaleType_Default, CollisionCategory_MoveLimit);
        pParts->mFollowMtx = pMtx;

        if (pParts != nullptr) {
            validateCollisionParts(pParts);
        }

        return pParts;
    }

    CollisionParts* tryCreateCollisionWaterSurface(LiveActor* pActor, MtxPtr pMtx, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "WaterSurface";
            kcl.format("%s.kcl", pKclName);
        }

        if (!getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "WaterSurface";
        mtx.set(pMtx);
        CollisionParts* pParts = ::createCollisionParts(getResourceHolder(pActor), pCollisionName, pSensor, mtx, CollisionScaleType_Default, CollisionCategory_WaterSurface);
        pParts->mFollowMtx = pMtx;

        if (pParts != nullptr) {
            validateCollisionParts(pParts);
        }

        return pParts;
    }

    CollisionParts* tryCreateCollisionSunshade(LiveActor* pActor, MtxPtr pMtx, HitSensor* pSensor) {
        MR::FormattedName kcl;
        {
            const char* pKclName = "Sunshade";
            kcl.format("%s.kcl", pKclName);
        }

        if (!getResourceHolder(pActor)->mFileInfoTable->isExistRes(kcl.c_str())) {
            return nullptr;
        }

        Pos3f mtx;
        const char* pCollisionName = "Sunshade";
        mtx.set(pMtx);
        CollisionParts* pParts = ::createCollisionParts(getResourceHolder(pActor), pCollisionName, pSensor, mtx, CollisionScaleType_Default, CollisionCategory_Sunshade);
        pParts->mFollowMtx = pMtx;

        if (pParts != nullptr) {
            validateCollisionParts(pParts);
        }

        return pParts;
    }

    CollisionParts* tryCreateCollisionAllOtherCategory(LiveActor* pActor, HitSensor* pSensor, CollisionParts** pMoveLimit,
                                                       CollisionParts** pWaterSurface, CollisionParts** pSunshade) {
        CollisionParts* moveLimit = tryCreateCollisionMoveLimit(pActor, pSensor);
        CollisionParts* waterSurface = tryCreateCollisionWaterSurface(pActor, pSensor);
        CollisionParts* sunshade = tryCreateCollisionSunshade(pActor, pSensor);

        if (pMoveLimit != nullptr) {
            *pMoveLimit = moveLimit;
        }

        if (pWaterSurface != nullptr) {
            *pWaterSurface = waterSurface;
        }

        if (pSunshade != nullptr) {
            *pSunshade = sunshade;
        }

        return sunshade;
    }

    CollisionParts* tryCreateCollisionAllOtherCategory(LiveActor* pActor, MtxPtr pMtx, HitSensor* pSensor, CollisionParts** pMoveLimit,
                                                       CollisionParts** pWaterSurface, CollisionParts** pSunshade) {
        CollisionParts* moveLimit = tryCreateCollisionMoveLimit(pActor, pMtx, pSensor);
        CollisionParts* waterSurface = tryCreateCollisionWaterSurface(pActor, pMtx, pSensor);
        CollisionParts* sunshade = tryCreateCollisionSunshade(pActor, pMtx, pSensor);

        if (pMoveLimit != nullptr) {
            *pMoveLimit = moveLimit;
        }

        if (pWaterSurface != nullptr) {
            *pWaterSurface = waterSurface;
        }

        if (pSunshade != nullptr) {
            *pSunshade = sunshade;
        }

        return sunshade;
    }

    bool isExistKcl(LiveActor* pActor, const char* pName) {
        MR::FormattedName kclName("%s.kcl", pName);
        return getResourceHolder(pActor)->mFileInfoTable->isExistRes(kclName.c_str());
    }

    f32 getCollisionBoundingSphereRange(const LiveActor* pActor) {
        return pActor->mCollisionParts->mBoundingRadius;
    }

    bool isValidCollisionParts(LiveActor* pActor) {
        return pActor->mCollisionParts->mIsValid;
    }

    void validateCollisionParts(LiveActor* pActor) {
        // An actor whose archive had no collision has none (LiveActor::initActorCollisionParts said so).
        if (pActor->mCollisionParts == nullptr) {
            return;
        }
        validateCollisionParts(pActor->mCollisionParts);
        CollisionParts* pParts = pActor->mCollisionParts;

        if (pParts->mFollowMtx != nullptr) {
            pParts->updateBoundingSphereRange();
        } else {
            pParts->updateBoundingSphereRange(pActor->mScale);
        }

        resetAllCollisionMtx(pActor);
    }

    void validateCollisionParts(CollisionParts* pParts) {
        if (!pParts->mIsValid) {
            pParts->addToBelongZone();
        }

        pParts->mIsValid = true;
    }

    void invalidateCollisionParts(LiveActor* pActor) {
        if (pActor->mCollisionParts == nullptr) {
            return;
        }
        invalidateCollisionParts(pActor->mCollisionParts);
    }

    void invalidateCollisionParts(CollisionParts* pParts) {
        if (pParts->mIsValid) {
            pParts->removeFromBelongZone();
        }

        pParts->mIsValid = false;
    }

    void validateExCollisionParts(LiveActor* pActor) {
        pActor->mBinder->mIsExCollisionValid = true;
    }

    void invalidateExCollisionParts(LiveActor* pActor) {
        pActor->mBinder->mIsExCollisionValid = false;
    }

    void onUpdateCollisionParts(LiveActor* pActor) {
        CollisionParts* pParts = pActor->mCollisionParts;

        if (!pParts->mIsValid) {
            validateCollisionParts(pActor);
        }

        pActor->mCollisionParts->mIsUpdate = true;
    }

    void onUpdateCollisionPartsOnetimeImmediately(LiveActor* pActor) {
        CollisionParts* pParts = pActor->mCollisionParts;

        if (!pParts->mIsValid) {
            validateCollisionParts(pActor);
        }

        pParts = pActor->mCollisionParts;
        pParts->mIsUpdateOnce = true;
        makeMtxTR(pActor->getBaseMtx(), pActor);
        setCollisionMtx(pActor, pActor->mCollisionParts);
        pParts->updateMtx();
    }

    void offUpdateCollisionParts(LiveActor* pActor) {
        CollisionParts* pParts = pActor->mCollisionParts;

        if (!pParts->mIsValid) {
            validateCollisionParts(pActor);
        }

        pActor->mCollisionParts->mIsUpdate = false;
    }

    void resetAllCollisionMtx(LiveActor* pActor) {
        CollisionParts* pParts = pActor->mCollisionParts;

        if (pParts->mFollowMtx != nullptr) {
            pParts->resetAllMtx();
            return;
        }

        Pos3f mtx;
        ::calcCollisionMtx(&mtx, pActor);
        pParts->resetAllMtx(mtx);
    }

    u32 getCollisionSensorType(const CollisionParts* pParts) {
        return pParts->mHitSensor->mType;
    }

    ModelObj* createModelObjMapObj(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_MapObj, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjMapObjStrongLight(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_MapObjStrongLight, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjNoSilhouettedMapObj(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_NoSilhouettedMapObj, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjNoSilhouettedMapObjStrongLight(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_NoSilhouettedMapObjStrongLight, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjIndirectMapObj(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_IndirectMapObj, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjPlayerDecoration(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_PlayerDecoration, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjEnemy(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_Enemy, MR::MovementType_EnemyDecoration, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjNpc(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_NPC, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjPlanetLow(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_PlanetLow, -2, -2, false);
        pObj->initWithoutIter();
        return pObj;
    }

    ModelObj* createModelObjBloomModel(const char* pName, const char* pModelName, MtxPtr pMtx) {
        ModelObj* pObj = new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_BloomModel, -2, -2, false);
        pObj->initWithoutIter();
        registerDemoSimpleCastAll(pObj);
        return pObj;
    }

    PartsModel* createPartsModelMapObj(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_MapObj, false);
        pModel->initWithoutIter();
        return pModel;
    }

    PartsModel* createPartsModelMapObjStrongLight(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_MapObjStrongLight, false);
        pModel->initWithoutIter();
        return pModel;
    }

    PartsModel* createPartsModelNoSilhouettedMapObj(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_NoSilhouettedMapObj, false);
        pModel->initWithoutIter();
        return pModel;
    }

    PartsModel* createPartsModelEnemy(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_Enemy, false);
        pModel->initWithoutIter();
        return pModel;
    }

    PartsModel* createPartsModelNpc(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_NPC, false);
        pModel->initWithoutIter();
        pModel->mIsShownWhileHostHidden = true;
        return pModel;
    }

    PartsModel* createPartsModelIndirectNpc(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_IndirectNpc, false);
        pModel->initWithoutIter();
        pModel->mIsShownWhileHostHidden = true;
        return pModel;
    }

    PartsModel* createPartsModelEnemyAndFix(LiveActor* pHost, const char* pName, const char* pModelName, MtxPtr pMtx, const Vec3f& rPos,
                                            const Vec3f& rRot, const char* pJointName) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, pMtx, MR::DrawBufferType_Enemy, false);
        pModel->initFixedPosition(rPos, rRot, pJointName);
        pModel->initWithoutIter();
        return pModel;
    }

    PartsModel* createPartsModelNpcAndFix(LiveActor* pHost, const char* pName, const char* pModelName, const char* pJointName) {
        PartsModel* pModel = new PartsModel(pHost, pName, pModelName, nullptr, MR::DrawBufferType_NPC, false);
        pModel->initFixedPosition(pJointName);
        pModel->initWithoutIter();
        pModel->mIsShownWhileHostHidden = true;
        return pModel;
    }

    LodCtrl* createLodCtrlNPC(LiveActor* pActor, const TableIter& rIter) {
        LodCtrl* pLod = new LodCtrl(pActor, rIter);
        return pLod;
    }

    LodCtrl* createLodCtrlPlanet(LiveActor* pActor, const TableIter& rIter, f32 farClip) {
        LodCtrl* pLod = new LodCtrl(pActor, rIter);
        pLod->createFarModel(MR::DrawBufferType_PlanetLow, -1, MR::CalcAnimType_Planet);
        pLod->setFarClipping(farClip);
        return pLod;
    }

    LodCtrl* createLodCtrlMapObj(LiveActor* pActor, const TableIter& rIter, f32 farClip) {
        LodCtrl* pLod = new LodCtrl(pActor, rIter);
        pLod->createFarModel(MR::DrawBufferType_MapObj, -1, MR::CalcAnimType_MapObj);
        pLod->setFarClipping(farClip);
        return pLod;
    }

    Flag* createMapFlag(const char* pName, const char* pInfoName, const Vec3f* pPos, const Vec3f& rSide, f32 stickLength, f32 width, f32 height,
                        s32 numPointsU, s32 numPointsV) {
        Flag* pFlag = new Flag(pName);
        pFlag->setInfoPos(pInfoName, pPos, rSide, stickLength, width, height, numPointsU, numPointsV);
        pFlag->initWithoutIter();

        return pFlag;
    }

    void stopSceneAtStep(const LiveActor* pActor, s32 step, s32 sceneStep) {
        if (isStep(pActor, step)) {
            stopScene(sceneStep);
        }
    }

    void tryRumblePadAndCameraDistanceVeryStrong(const LiveActor* pActor, f32 near, f32 middle, f32 far) {
        if (isNearPlayerAnyTime(pActor, near)) {
            tryRumblePadVeryStrong(pActor, PLAYER_1);
            shakeCameraStrong();
        } else if (isNearPlayerAnyTime(pActor, middle)) {
            tryRumblePadStrong(pActor, PLAYER_1);
            shakeCameraNormal();
        } else if (isNearPlayerAnyTime(pActor, far)) {
            tryRumblePadMiddle(pActor, PLAYER_1);
            shakeCameraNormalWeak();
        }
    }

    void tryRumblePadAndCameraDistanceStrong(const LiveActor* pActor, f32 near, f32 middle, f32 far) {
        if (isNearPlayerAnyTime(pActor, near)) {
            tryRumblePadStrong(pActor, PLAYER_1);
            shakeCameraNormal();
        } else if (isNearPlayerAnyTime(pActor, middle)) {
            tryRumblePadMiddle(pActor, PLAYER_1);
            shakeCameraNormalWeak();
        } else if (isNearPlayerAnyTime(pActor, far)) {
            tryRumblePadWeak(pActor, PLAYER_1);
            shakeCameraWeak();
        }
    }

    void tryRumblePadAndCameraDistanceMiddle(const LiveActor* pActor, f32 near, f32 middle, f32 far) {
        if (isNearPlayerAnyTime(pActor, near)) {
            tryRumblePadMiddle(pActor, PLAYER_1);
            shakeCameraNormalWeak();
        } else if (isNearPlayerAnyTime(pActor, middle)) {
            tryRumblePadWeak(pActor, PLAYER_1);
            shakeCameraWeak();
        } else if (isNearPlayerAnyTime(pActor, far)) {
            tryRumblePadVeryWeak(pActor, PLAYER_1);
            shakeCameraVeryWeak();
        }
    }

    bool isCalcGravity(const LiveActor* pActor) {
        return pActor->mFlag.mIsCalcGravity;
    }

    void onCalcGravity(LiveActor* pActor) {
        if (!isDead(pActor)) {
            calcGravity(pActor);
        }

        pActor->mFlag.mIsCalcGravity = true;
    }

    void offCalcGravity(LiveActor* pActor) {
        pActor->mFlag.mIsCalcGravity = false;
    }

    void setCollisionMtx(LiveActor* pActor) {
        setCollisionMtx(pActor, getCollisionParts(pActor));
    }

    void setCollisionMtx(LiveActor* pActor, CollisionParts* pParts) {
        if (!pParts->mIsValid) {
            return;
        }

        if (pParts->mFollowMtx != nullptr) {
            pParts->setMtx();
            return;
        }

        Pos3f mtx;
        ::calcCollisionMtx(&mtx, pActor);
        pParts->setMtx(mtx);
    }

    CollisionParts* getCollisionParts(const LiveActor* pActor) {
        return pActor->mCollisionParts;
    }

    bool isExistCollisionParts(const LiveActor* pActor) {
        return pActor->mCollisionParts != nullptr;
    }
};  // namespace MR
