#include "Game/AreaObj/AreaForm.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getBaseSize_f1, "getBaseSize.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f1, "updateBoxParam.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f2, "updateBoxParam.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f3, "updateBoxParam.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f4, "updateBoxParam.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f5, "updateBoxParam.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f6, "updateBoxParam.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBoxParam_f7, "updateBoxParam.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCenterPos_f1, "calcCenterPos.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 500.0f);

AreaFormCube::AreaFormCube(int cubeType) {
    mCubeType = cubeType;
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mRotation.x = 0.0f;
    mRotation.y = 0.0f;
    mRotation.z = 0.0f;
    mScale.x = 1.0f;
    mScale.y = 1.0f;
    mScale.z = 1.0f;
    mBounding.i.x = 0.0f;
    mBounding.i.y = 0.0f;
    mBounding.i.z = 0.0f;
    mBounding.f.x = 0.0f;
    mBounding.f.y = 0.0f;
    mBounding.f.z = 0.0f;
}

void AreaFormCube::init(const TableIter& rIter) {
    MR::getPlacementTrans(rIter, &mTranslation);
    MR::getPlacementRotate(rIter, &mRotation);
    MR::getPlacementScale(rIter, &mScale);
    updateBoxParam();
}

bool AreaFormCube::isInVolume(const Vec3f& rPos) const {
    Pos3f pos;
    calcWorldMtx(&pos);

    Vec3f transpose;
    pos.multTranspose(rPos, transpose);

    return transpose.x >= mBounding.i.x && transpose.y >= mBounding.i.y && transpose.z >= mBounding.i.z && transpose.x < mBounding.f.x &&
           transpose.y < mBounding.f.y && transpose.z < mBounding.f.z;
}

void AreaFormCube::calcWorldPos(Vec3f* pPos) const {
    if (mFollowMtx != nullptr) {
        mFollowMtx->mult(mTranslation, *pPos);
    } else {
        pPos->set(mTranslation);
    }
}

void AreaFormCube::calcWorldRotate(Vec3f* pOut) const {
    const Pos3f* pFollowMtx = mFollowMtx;
    if (pFollowMtx != nullptr) {
        Pos3f stack = mBaseMtx;
        stack.zeroTrans();
        stack.concat(*pFollowMtx, stack);
        stack.getEulerXYZ(*pOut);
        *pOut = *pOut * _180_PI;
    } else {
        pOut->set(mRotation);
    }
}

void AreaFormCube::calcWorldBox(DirBox3f* pBox) const {
    Pos3f pos;
    calcWorldMtx(&pos);

    pBox->mAxisX.x = pos.mMtx[0][0];
    pBox->mAxisY.x = pos.mMtx[0][1];
    pBox->mAxisZ.x = pos.mMtx[0][2];
    pBox->mAxisX.y = pos.mMtx[1][0];
    pBox->mAxisY.y = pos.mMtx[1][1];
    pBox->mAxisZ.y = pos.mMtx[1][2];
    pBox->mAxisX.z = pos.mMtx[2][0];
    pBox->mAxisY.z = pos.mMtx[2][1];
    pBox->mAxisZ.z = pos.mMtx[2][2];

    pBox->mSize.sub(mBounding.f, mBounding.i);
    pos.mult(mBounding.i, pBox->mCorner);
}

void AreaFormCube::calcLocalPos(Vec3f* pPos, const Vec3f& rPos) const {
    Pos3f worldMtx;
    calcWorldMtx(&worldMtx);
    worldMtx.multTranspose(rPos, *pPos);
}

f32 AreaFormCube::getBaseSize() {
    return cNum_getBaseSize_f1;
}

void AreaFormCube::updateBoxParam() {
    MR::makeMtxRotate(mBaseMtx, mRotation.x, mRotation.y, mRotation.z);

    mBaseMtx.setTrans(mTranslation);

    Vec3f boxMin(cNum_updateBoxParam_f1 * (-mScale.x * getBaseSize()), cNum_updateBoxParam_f2 * (-mScale.y * getBaseSize()), cNum_updateBoxParam_f3 * (-mScale.z * getBaseSize()));
    Vec3f boxMax(cNum_updateBoxParam_f4 * (mScale.x * getBaseSize()), cNum_updateBoxParam_f5 * (mScale.y * getBaseSize()), cNum_updateBoxParam_f6 * (mScale.z * getBaseSize()));

    mBounding.i.set(boxMin);
    mBounding.f.set(boxMax);

    if (mCubeType == 1) {
        Vec3f halfHeight(0.0f, cNum_updateBoxParam_f7 * (mScale.y * getBaseSize()), 0.0f);

        mBounding.i += halfHeight;
        mBounding.f += halfHeight;
    }
}

void AreaFormCube::calcWorldMtx(Pos3f* pPos) const {
    const Pos3f* pFollowMtx = mFollowMtx;
    if (pFollowMtx != nullptr) {
        pPos->concat(*pFollowMtx, mBaseMtx);
    } else {
        pPos->set(mBaseMtx);
    }
}

AreaFormSphere::AreaFormSphere() {
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mRadius = 0.0f;
}

void AreaFormSphere::calcUpVec(Vec3f* pOut) const {
    if (mFollowMtx != nullptr) {
        mFollowMtx->mult33(mUp, *pOut);
        MR::normalize(pOut);
    } else {
        pOut->set(mUp);
    }
}

void AreaFormSphere::calcPos(Vec3f* pOut) const {
    if (mFollowMtx != nullptr) {
        mFollowMtx->mult(mTranslation, *pOut);
    } else {
        pOut->set(mTranslation);
    }
}

bool AreaFormSphere::isInVolume(const Vec3f& rVector) const {
    Vec3f pos;
    calcPos(&pos);

    return (rVector - pos).length() < mRadius;
}

AreaFormBowl::AreaFormBowl() {
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mRadius = 0.0f;
}

void AreaFormBowl::init(const TableIter& rIter) {
    MR::getPlacementTrans(rIter, &mTranslation);

    // 1 for a table without the column.
    f32 scale_x = 1.0f;
    rIter.getValue< f32 >("scale_x", &scale_x);
    mRadius = scale_x;
    mRadius *= cNum_init_f1;

    Vec3f rotation;
    MR::getPlacementRotate(rIter, &rotation);
    calcUpVec(rotation);
}

bool AreaFormBowl::isInVolume(const Vec3f& rPos) const {
    if ((rPos - mTranslation).length() > mRadius) {
        return false;
    }

    Vec3f relative(rPos - mTranslation);
    return relative.dot(mUp) < 0.0f;
}

void AreaFormBowl::calcUpVec(const Vec3f& rPos) {
    Vec3f up(0.0f, 1.0f, 0.0f);

    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rPos.x, rPos.y, rPos.z);
    NebulaMtxMultVec(mtx, &up, &up);
    mUp = up;
}

AreaFormCylinder::AreaFormCylinder() {
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mUp.x = 0.0f;
    mUp.y = 0.0f;
    mUp.z = 0.0f;
    mRadius = 0.0f;
    mHeight = 0.0f;
}

void AreaFormCylinder::calcPos(Vec3f* pPos) const {
    if (mFollowMtx != nullptr) {
        mFollowMtx->mult(mTranslation, *pPos);
    } else {
        pPos->set(mTranslation);
    }
}

void AreaFormCylinder::calcCenterPos(Vec3f* pCenterPos) const {
    calcPos(pCenterPos);

    Vec3f upVec;
    calcUpVec(&upVec);
    pCenterPos->add(upVec * mHeight * cNum_calcCenterPos_f1);
}

void AreaFormCylinder::calcUpVec(Vec3f* pUpVec) const {
    if (mFollowMtx != nullptr) {
        mFollowMtx->mult33(mUp, *pUpVec);
        MR::normalize(pUpVec);
    } else {
        pUpVec->set(mUp);
    }
}

bool AreaFormCylinder::isInVolume(const Vec3f& rVec) const {
    Vec3f pos;
    calcPos(&pos);

    Vec3f up;
    calcUpVec(&up);

    Vec3f radial = rVec - pos;
    f32 height = MR::vecKillElement(radial, up, &radial);
    f32 radialDist = radial.length();

    return MR::isInRange(height, 0.0f, mHeight) && radialDist < mRadius;
}

void AreaFormCylinder::calcDir(const Vec3f& rRotation) {
    mUp.x = 0.0f;
    mUp.y = 1.0f;
    mUp.z = 0.0f;

    MtxPtr mtx_x = MR::tmpMtxRotXDeg(rRotation.x);
    NebulaMtxMultVec(mtx_x, &mUp, &mUp);
    MtxPtr mtx_y = MR::tmpMtxRotYDeg(rRotation.y);
    NebulaMtxMultVec(mtx_y, &mUp, &mUp);
    MtxPtr mtx_z = MR::tmpMtxRotZDeg(rRotation.z);
    NebulaMtxMultVec(mtx_z, &mUp, &mUp);
    mUp.normalize();
}

void AreaFormSphere::init(const TableIter& rIter) {
    MR::getPlacementTrans(rIter, &mTranslation);

    f32 scale_x = 1.0f;
    rIter.getValue< f32 >("scale_x", &scale_x);

    mRadius = scale_x;
    mRadius *= cNum_init_f2;

    Vec3f rotation;
    MR::getPlacementRotate(rIter, &rotation);

    Vec3f up(0.0f, 1.0f, 0.0f);

    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rotation.x, rotation.y, rotation.z);
    NebulaMtxMultVec(mtx, &up, &up);
    mUp = up;
}

void AreaFormCylinder::init(const TableIter& rIter) {
    MR::getPlacementTrans(rIter, &mTranslation);
    Vec3f rotate;

    MR::getPlacementRotate(rIter, &rotate);
    calcDir(rotate);

    f32 scale = 1.0f;
    rIter.getValue< f32 >("scale_x", &scale);

    mRadius = scale;
    mRadius *= cNum_init_f3;

    rIter.getValue< f32 >("scale_y", &scale);
    mHeight = scale;
    mHeight *= cNum_init_f4;
}
