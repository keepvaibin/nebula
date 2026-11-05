#include "Game/AreaObj/AreaForm.hpp"
#include "Game/Util.hpp"

AreaFormCube::AreaFormCube(int a1) {
    _8 = a1;
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

void AreaFormCube::init(const JMapInfoIter& rIter) {
    MR::getJMapInfoTrans(rIter, &mTranslation);
    MR::getJMapInfoRotate(rIter, &mRotation);
    MR::getJMapInfoScale(rIter, &mScale);
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
    if (_4 != nullptr) {
        _4->mult(mTranslation, *pPos);
    } else {
        pPos->set(mTranslation);
    }
}

void AreaFormCube::calcWorldRotate(Vec3f* pOut) const {
    const Pos3f* pFollowMtx = _4;
    if (pFollowMtx != nullptr) {
        Pos3f stack = _48;
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

    pBox->_0.x = pos.mMtx[0][0];
    pBox->_C.x = pos.mMtx[0][1];
    pBox->_18.x = pos.mMtx[0][2];
    pBox->_24.x = pos.mMtx[0][3];
    pBox->_0.y = pos.mMtx[1][0];
    pBox->_C.y = pos.mMtx[1][1];
    pBox->_18.y = pos.mMtx[1][2];
    pBox->_24.y = pos.mMtx[1][3];
    pBox->_0.z = pos.mMtx[2][0];
    pBox->_C.z = pos.mMtx[2][1];
    pBox->_18.z = pos.mMtx[2][2];
    pBox->_24.z = pos.mMtx[2][3];

    pBox->_30.sub(mBounding.f, mBounding.i);
    pos.mult(mBounding.i, pBox->_24);
}

void AreaFormCube::calcLocalPos(Vec3f* pPos, const Vec3f& rPos) const {
    Pos3f worldMtx;
    calcWorldMtx(&worldMtx);
    worldMtx.multTranspose(rPos, *pPos);
}

f32 AreaFormCube::getBaseSize() {
    return 1000.0f;
}

void AreaFormCube::updateBoxParam() {
    MR::makeMtxRotate(_48, mRotation.x, mRotation.y, mRotation.z);

    _48.setTrans(mTranslation);

    Vec3f v10(0.5f * (-mScale.x * getBaseSize()), 0.5f * (-mScale.y * getBaseSize()), 0.5f * (-mScale.z * getBaseSize()));
    Vec3f v9(0.5f * (mScale.x * getBaseSize()), 0.5f * (mScale.y * getBaseSize()), 0.5f * (mScale.z * getBaseSize()));

    mBounding.i.set(v10);
    mBounding.f.set(v9);

    if (_8 == 1) {
        Vec3f temp(0.0f, 0.5f * (mScale.y * getBaseSize()), 0.0f);

        mBounding.i += temp;
        mBounding.f += temp;
    }
}

void AreaFormCube::calcWorldMtx(Pos3f* pPos) const {
    const Pos3f* pFollowMtx = _4;
    if (pFollowMtx != nullptr) {
        pPos->concat(*pFollowMtx, _48);
    } else {
        pPos->set(_48);
    }
}

AreaFormSphere::AreaFormSphere() {
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mRadius = 0.0f;
}

void AreaFormSphere::calcUpVec(Vec3f* pOut) const {
    if (_4 != nullptr) {
        _4->mult33(mUp, *pOut);
        MR::normalize(pOut);
    } else {
        pOut->set(mUp);
    }
}

void AreaFormSphere::calcPos(Vec3f* pOut) const {
    if (_4 != nullptr) {
        _4->mult(mTranslation, *pOut);
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
    _20 = 0.0f;
}

void AreaFormBowl::init(const JMapInfoIter& rIter) {
    MR::getJMapInfoTrans(rIter, &mTranslation);

    f32 scale_x;
    rIter.getValue< f32 >("scale_x", &scale_x);
    _20 = scale_x;
    _20 *= 500.0f;

    Vec3f rotation;
    MR::getJMapInfoRotate(rIter, &rotation);
    calcUpVec(rotation);
}

bool AreaFormBowl::isInVolume(const Vec3f& rPos) const {
    if ((rPos - mTranslation).length() > _20) {
        return false;
    }

    Vec3f relative(rPos - mTranslation);
    return relative.dot(mUp) < 0.0f;
}

void AreaFormBowl::calcUpVec(const Vec3f& rPos) {
    Vec3f up(0.0f, 1.0f, 0.0f);

    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rPos.x, rPos.y, rPos.z);
    NebulaMtxMultVec(mtx, (const Vec*)&up, (Vec*)&up);
    mUp = up;
}

AreaFormCylinder::AreaFormCylinder() {
    mTranslation.x = 0.0f;
    mTranslation.y = 0.0f;
    mTranslation.z = 0.0f;
    mRotation.x = 0.0f;
    mRotation.y = 0.0f;
    mRotation.z = 0.0f;
    mRadius = 0.0f;
    mHeight = 0.0f;
}

void AreaFormCylinder::calcPos(Vec3f* pPos) const {
    if (_4 != nullptr) {
        _4->mult(mTranslation, *pPos);
    } else {
        pPos->set(mTranslation);
    }
}

void AreaFormCylinder::calcCenterPos(Vec3f* pCenterPos) const {
    calcPos(pCenterPos);

    Vec3f upVec;
    calcUpVec(&upVec);
    pCenterPos->add(upVec * mHeight * 0.5f);
}

void AreaFormCylinder::calcUpVec(Vec3f* pUpVec) const {
    if (_4 != nullptr) {
        _4->mult33(mRotation, *pUpVec);
        MR::normalize(pUpVec);
    } else {
        pUpVec->set(mRotation);
    }
}

bool AreaFormCylinder::isInVolume(const Vec3f& rVec) const {
    Vec3f pos;
    calcPos(&pos);

    Vec3f up;
    calcUpVec(&up);

    Vec3f stack_8 = rVec - pos;
    f32 v6 = MR::vecKillElement(stack_8, up, &stack_8);
    f32 v7 = stack_8.length();

    bool ret;

    if (MR::isInRange(v6, 0.0f, mHeight) && (v7 < mRadius)) {
        ret = true;
    } else {
        ret = false;
    }

    return ret;
}

void AreaFormCylinder::calcDir(const Vec3f& rRotation) {
    mRotation.x = 0.0f;
    mRotation.y = 1.0f;
    mRotation.z = 0.0f;

    MtxPtr mtx_x = MR::tmpMtxRotXDeg(rRotation.x);
    NebulaMtxMultVec(mtx_x, (const Vec*)&mRotation, (Vec*)&mRotation);
    MtxPtr mtx_y = MR::tmpMtxRotYDeg(rRotation.y);
    NebulaMtxMultVec(mtx_y, (const Vec*)&mRotation, (Vec*)&mRotation);
    MtxPtr mtx_z = MR::tmpMtxRotZDeg(rRotation.z);
    NebulaMtxMultVec(mtx_z, (const Vec*)&mRotation, (Vec*)&mRotation);
    mRotation.normalize();
}

void AreaFormSphere::init(const JMapInfoIter& rIter) {
    MR::getJMapInfoTrans(rIter, &mTranslation);

    f32 scale_x;
    rIter.getValue< f32 >("scale_x", &scale_x);

    mRadius = scale_x;
    mRadius *= 500.0f;

    Vec3f rotation;
    MR::getJMapInfoRotate(rIter, &rotation);

    Vec3f up(0.0f, 1.0f, 0.0f);

    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rotation.x, rotation.y, rotation.z);
    NebulaMtxMultVec(mtx, (const Vec*)&up, (Vec*)&up);
    mUp = up;
}

void AreaFormCylinder::init(const JMapInfoIter& rIter) {
    MR::getJMapInfoTrans(rIter, &mTranslation);
    Vec3f rotate;

    MR::getJMapInfoRotate(rIter, &rotate);
    calcDir(rotate);

    f32 temp;
    rIter.getValue< f32 >("scale_x", &temp);

    mRadius = temp;
    mRadius *= 500.0f;

    rIter.getValue< f32 >("scale_y", &temp);
    mHeight = temp;
    mHeight *= 500.0f;
}
