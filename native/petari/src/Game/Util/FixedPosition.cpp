#include "Game/Util/FixedPosition.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

FixedPosition::FixedPosition(const LiveActor* pActor, const char* pJointName, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
    init(MR::getJointMtx(pActor, pJointName), rLocalTrans, rLocalRotate);
}

FixedPosition::FixedPosition(const LiveActor* pActor, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
    init(pActor->getBaseMtx(), rLocalTrans, rLocalRotate);
}

FixedPosition::FixedPosition(MtxPtr pBaseMtx, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
    init(pBaseMtx, rLocalTrans, rLocalRotate);
}

void FixedPosition::calc() {
    mMtx.identity();
    mMtx.setRotate(mLocalRotate * PI_180);
    mMtx.setTrans(mLocalTrans);

    if (mBaseMtx != nullptr) {
        Mtx34f baseMtx;
        baseMtx.set(mBaseMtx);
        mMtx.concat(baseMtx, mMtx);
    }

    if (mNormalizeScale) {
        MR::makeMtxWithoutScale(&mMtx, mMtx);
    }
}

void FixedPosition::setBaseMtx(MtxPtr pBaseMtx) {
    mBaseMtx = pBaseMtx;
}

void FixedPosition::setLocalTrans(const Vec3f& rLocalTrans) {
    mLocalTrans.set(rLocalTrans);
}

void FixedPosition::init(MtxPtr pBaseMtx, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
    setBaseMtx(pBaseMtx);
    mLocalTrans.set(rLocalTrans);
    mLocalRotate.set(rLocalRotate);
    mMtx.identity();
    mNormalizeScale = true;
}

FixedPosition::FixedPosition(const LiveActor* pActor, const char* pResName, const LiveActor* pResActor) {
    if (pResActor == nullptr) {
        pResActor = pActor;
    }

    ResourceHolder* resourceHolder = MR::getResourceHolder(pResActor);
    Table* csv = MR::tryCreateCsvParser(resourceHolder, "%s.bcsv", pResName);

    const char* jointName = nullptr;
    Vec3f trans(0.0f, 0.0f, 0.0f);
    Vec3f rotate(0.0f, 0.0f, 0.0f);

    MR::getCsvDataStrOrNULL(&jointName, csv, "JointName", 0);
    MR::getCsvDataVec(&trans, csv, "Trans", 0);
    MR::getCsvDataVec(&rotate, csv, "Rotate", 0);

    if (jointName == nullptr) {
        init(pActor->getBaseMtx(), trans, rotate);
    } else {
        init(MR::getJointMtx(pActor, jointName), trans, rotate);
    }
}
