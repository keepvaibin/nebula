#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cDummyMtxRegName = "ダミーマトリクス";
    const char* const cDummyVecRegName = "ダミーベクトル";
    const char* const cDummyFloatRegName = "ダミー浮動小数";
}  // namespace

CameraRegisterHolder::CameraRegisterHolder(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< CameraRegisterHolder >);
    mDummyMatrix.identity();
    declareMtxReg(::cDummyMtxRegName, mDummyMatrix.toMtxPtr());

    mDummyVector.zero();
    declareVecReg(::cDummyVecRegName, &mDummyVector);

    mDummyFloat = 0.0f;
    declareFloatReg(::cDummyFloatRegName, &mDummyFloat);
}

void CameraRegisterHolder::declareMtxReg(const char* pName, MtxPtr pValue) {
    mMatrices.declare(pName, pValue);
}

MtxPtr CameraRegisterHolder::getMtx(const char* pName) const {
    return mMatrices.get(pName);
}

void CameraRegisterHolder::declareVecReg(const char* pName, const Vec3f* pValue) {
    mVectors.declare(pName, pValue);
}

const Vec3f* CameraRegisterHolder::getVec(const char* pName) const {
    return mVectors.get(pName);
}

void CameraRegisterHolder::declareFloatReg(const char* pName, const f32* pValue) {
    mFloats.declare(pName, pValue);
}

const char* CameraRegisterHolder::getDummyVecRegName() const {
    return ::cDummyVecRegName;
}
