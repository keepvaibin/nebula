#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>


static Mtx mtrans_org = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

static Mtx tmpmtx_sc = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

static Mtx tmpmtx_rx = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

static Mtx tmpmtx_ry = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

static Mtx tmpmtx_rz = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

namespace MR {
    void makeMtxRotate(MtxPtr pMtx, s16 rx, s16 ry, s16 rz) {
        f32 sinX, sinY, sinZ, cosX, cosY, cosZ;
        Angle::sinCosShort(rx, &sinX, &cosX);
        Angle::sinCosShort(ry, &sinY, &cosY);
        Angle::sinCosShort(rz, &sinZ, &cosZ);

        pMtx[0][0] = cosZ * cosY;
        pMtx[1][0] = sinZ * cosY;
        pMtx[2][0] = -sinY;

        pMtx[0][1] = cosZ * sinY * sinX - sinZ * cosX;
        pMtx[1][1] = sinZ * sinY * sinX + cosZ * cosX;
        pMtx[2][1] = cosY * sinX;

        pMtx[0][2] = cosZ * sinY * cosX + sinZ * sinX;
        pMtx[1][2] = sinZ * sinY * cosX - cosZ * sinX;
        pMtx[2][2] = cosY * cosX;

        pMtx[0][3] = 0.0f;
        pMtx[1][3] = 0.0f;
        pMtx[2][3] = 0.0f;
    }

    void makeMtxRotate(MtxPtr pMtx, f32 rx, f32 ry, f32 rz) {
        s16 angleX = (s16)(s32)((rx * DEGREE_TO_S16));
        s16 angleY = (s16)(s32)((ry * DEGREE_TO_S16));
        s16 angleZ = (s16)(s32)((rz * DEGREE_TO_S16));
        makeMtxRotate(pMtx, angleX, angleY, angleZ);
    }

    void makeMtxRotate(MtxPtr pMtx, const Vec3f& rVec) {
        makeMtxRotate(pMtx, rVec.x, rVec.y, rVec.z);
    }

    void makeMtxRotateY(MtxPtr pMtx, f32 ry) {
        s16 angle = (s16)(s32)((ry * DEGREE_TO_S16));
        f32 sinY, cosY;
        Angle::sinCosShort(angle, &sinY, &cosY);

        pMtx[0][0] = cosY;
        pMtx[1][0] = 0.0f;
        pMtx[2][0] = -sinY;
        pMtx[0][1] = 0.0f;
        pMtx[1][1] = 1.0f;
        pMtx[2][1] = 0.0f;
        pMtx[0][2] = sinY;
        pMtx[1][2] = 0.0f;
        pMtx[2][2] = cosY;
        pMtx[0][3] = 0.0f;
        pMtx[1][3] = 0.0f;
        pMtx[2][3] = 0.0f;
    }

    void makeMtxTR(MtxPtr pMtx, f32 tx, f32 ty, f32 tz, f32 rx, f32 ry, f32 rz) {
        f32 sinX = Angle::sinDegree(rx);
        f32 sinY = Angle::sinDegree(ry);
        f32 sinZ = Angle::sinDegree(rz);
        f32 cosX = Angle::cosDegree(rx);
        f32 cosY = Angle::cosDegree(ry);
        f32 cosZ = Angle::cosDegree(rz);

        pMtx[0][0] = cosZ * cosY;
        pMtx[1][0] = sinZ * cosY;
        pMtx[2][0] = -sinY;

        pMtx[0][1] = cosZ * sinY * sinX - sinZ * cosX;
        pMtx[1][1] = sinZ * sinY * sinX + cosZ * cosX;
        pMtx[2][1] = cosY * sinX;

        pMtx[0][2] = cosZ * sinY * cosX + sinZ * sinX;
        pMtx[1][2] = sinZ * sinY * cosX - cosZ * sinX;
        pMtx[2][2] = cosY * cosX;

        pMtx[0][3] = tx;
        pMtx[1][3] = ty;
        pMtx[2][3] = tz;
    }

    void makeMtxTR(MtxPtr pMtx, const Vec3f& rTrans, const Vec3f& rRot) {
        makeMtxTR(pMtx, rTrans.x, rTrans.y, rTrans.z, rRot.x, rRot.y, rRot.z);
    }

    void makeMtxTransRotateY(MtxPtr pMtx, f32 tx, f32 ty, f32 tz, f32 ry) {
        makeMtxRotateY(pMtx, ry);
        pMtx[0][3] = tx;
        pMtx[1][3] = ty;
        pMtx[2][3] = tz;
    }

    void rotateMtxLocalX(MtxPtr pMtx, f32 rad) {
        f32 cosAngle = Angle::cosRadian(rad);
        f32 sinAngle = Angle::sinRadian(rad);
        f32 y0 = cosAngle * pMtx[0][1] - sinAngle * pMtx[0][2];
        f32 y1 = cosAngle * pMtx[1][1] - sinAngle * pMtx[1][2];
        f32 y2 = cosAngle * pMtx[2][1] - sinAngle * pMtx[2][2];
        f32 z0 = sinAngle * pMtx[0][1] + cosAngle * pMtx[0][2];
        f32 z1 = sinAngle * pMtx[1][1] + cosAngle * pMtx[1][2];
        f32 z2 = sinAngle * pMtx[2][1] + cosAngle * pMtx[2][2];
        pMtx[0][1] = y0;
        pMtx[1][1] = y1;
        pMtx[2][1] = y2;
        pMtx[0][2] = z0;
        pMtx[1][2] = z1;
        pMtx[2][2] = z2;
    }

    void rotateMtxLocalY(MtxPtr pMtx, f32 rad) {
        f32 cosAngle = Angle::cosRadian(rad);
        f32 sinAngle = Angle::sinRadian(rad);
        f32 x0 = cosAngle * pMtx[0][0] - sinAngle * pMtx[0][2];
        f32 x1 = cosAngle * pMtx[1][0] - sinAngle * pMtx[1][2];
        f32 x2 = cosAngle * pMtx[2][0] - sinAngle * pMtx[2][2];
        f32 z0 = sinAngle * pMtx[0][0] + cosAngle * pMtx[0][2];
        f32 z1 = sinAngle * pMtx[1][0] + cosAngle * pMtx[1][2];
        f32 z2 = sinAngle * pMtx[2][0] + cosAngle * pMtx[2][2];
        pMtx[0][0] = x0;
        pMtx[1][0] = x1;
        pMtx[2][0] = x2;
        pMtx[0][2] = z0;
        pMtx[1][2] = z1;
        pMtx[2][2] = z2;
    }

    void rotateMtxLocalZ(MtxPtr pMtx, f32 rad) {
        f32 cosAngle = Angle::cosRadian(rad);
        f32 sinAngle = Angle::sinRadian(rad);
        f32 x0 = cosAngle * pMtx[0][0] - sinAngle * pMtx[0][1];
        f32 x1 = cosAngle * pMtx[1][0] - sinAngle * pMtx[1][1];
        f32 x2 = cosAngle * pMtx[2][0] - sinAngle * pMtx[2][1];
        f32 y0 = sinAngle * pMtx[0][0] + cosAngle * pMtx[0][1];
        f32 y1 = sinAngle * pMtx[1][0] + cosAngle * pMtx[1][1];
        f32 y2 = sinAngle * pMtx[2][0] + cosAngle * pMtx[2][1];
        pMtx[0][0] = x0;
        pMtx[1][0] = x1;
        pMtx[2][0] = x2;
        pMtx[0][1] = y0;
        pMtx[1][1] = y1;
        pMtx[2][1] = y2;
    }

    void rotateMtxLocalXDegree(MtxPtr pMtx, f32 deg) {
        rotateMtxLocalX(pMtx, deg * PI_180);
    }

    void rotateMtxLocalYDegree(MtxPtr pMtx, f32 deg) {
        rotateMtxLocalY(pMtx, deg * PI_180);
    }

    void rotateMtxLocalZDegree(MtxPtr pMtx, f32 deg) {
        rotateMtxLocalZ(pMtx, deg * PI_180);
    }

    void preScaleMtx(MtxPtr pMtx, f32 scale) {
        preScaleMtx(pMtx, scale, scale, scale);
    }

    void preScaleMtx(MtxPtr pMtx, const Vec3f& rScale) {
        preScaleMtx(pMtx, rScale.x, rScale.y, rScale.z);
    }

    void preScaleMtx(MtxPtr pMtx, f32 sx, f32 sy, f32 sz) {
        pMtx[0][0] *= sx;
        pMtx[1][0] *= sx;
        pMtx[2][0] *= sx;
        pMtx[0][1] *= sy;
        pMtx[1][1] *= sy;
        pMtx[2][1] *= sy;
        pMtx[0][2] *= sz;
        pMtx[1][2] *= sz;
        pMtx[2][2] *= sz;
    }

    void scaleMtxToLocalMtx(MtxPtr pDst, MtxPtr pSrc, MtxPtr pBase, const Vec3f& rScale) {
        Pos3f baseCopy;
        baseCopy.setInline(pBase);
        Vec3f srcTrans;
        baseCopy.setTrans(Vec3f(0.0f, 0.0f, 0.0f));

        Pos3f invMtx;
        invMtx.invert(baseCopy);
        preScaleMtx(baseCopy, rScale.x, rScale.y, rScale.z);

        extractMtxTrans(pSrc, &srcTrans);
        multMtx(pDst, pSrc, invMtx);
        multMtx(pDst, pDst, baseCopy);
        setMtxTrans(pDst, srcTrans.x, srcTrans.y, srcTrans.z);
    }

    void scaleMtxToLocalMtx(MtxPtr pDst, MtxPtr pSrc, const Vec3f& rScale) {
        scaleMtxToLocalMtx(pDst, pDst, pSrc, rScale);
    }

    void scaleMtxToDir(Pos3f* pDst, const Vec3f& rDir, const Vec3f& rScale) {
        Vec3f axisX, axisY;
        MR::makeAxisCrossPlane(&axisX, &axisY, rDir);

        // basis * scale * basis^T: rScale applied along (axisX, rDir, axisY).
        Pos3f basis;
        basis.identity();
        Pos3f basisT;
        basisT.identity();

        basis.setXYZDir(axisX, rDir, axisY);

        basisT.mMtx[0][0] = axisX.x;
        basisT.mMtx[0][1] = axisX.y;
        basisT.mMtx[0][2] = axisX.z;
        basisT.mMtx[1][0] = rDir.x;
        basisT.mMtx[1][1] = rDir.y;
        basisT.mMtx[1][2] = rDir.z;
        basisT.mMtx[2][0] = axisY.x;
        basisT.mMtx[2][1] = axisY.y;
        basisT.mMtx[2][2] = axisY.z;

        pDst->zeroTrans();
        pDst->setScale(rScale.x, rScale.y, rScale.z);
        pDst->concat(*pDst, basisT);
        pDst->concat(basis, *pDst);
    }

    void makeMtxTRS(MtxPtr pMtx, f32 tx, f32 ty, f32 tz, f32 rx, f32 ry, f32 rz, f32 sx, f32 sy, f32 sz) {
        f32 sinX = Angle::sinDegree(rx);
        f32 sinY = Angle::sinDegree(ry);
        f32 sinZ = Angle::sinDegree(rz);
        f32 cosX = Angle::cosDegree(rx);
        f32 cosY = Angle::cosDegree(ry);
        f32 cosZ = Angle::cosDegree(rz);

        pMtx[0][0] = sx * (cosZ * cosY);
        pMtx[1][0] = sx * (sinZ * cosY);
        pMtx[2][0] = -sinY * sx;

        pMtx[0][1] = sy * (cosZ * sinY * sinX - sinZ * cosX);
        pMtx[1][1] = sy * (sinZ * sinY * sinX + cosZ * cosX);
        pMtx[2][1] = sy * (cosY * sinX);

        pMtx[0][2] = sz * (cosZ * sinY * cosX + sinZ * sinX);
        pMtx[1][2] = sz * (sinZ * sinY * cosX - cosZ * sinX);
        pMtx[2][2] = sz * (cosY * cosX);

        pMtx[0][3] = tx;
        pMtx[1][3] = ty;
        pMtx[2][3] = tz;
    }

    void makeMtxTRS(MtxPtr pMtx, const Vec3f& rTrans, const Vec3f& rRot, const Vec3f& rScale) {
        makeMtxTRS(pMtx, rTrans.x, rTrans.y, rTrans.z, rRot.x, rRot.y, rRot.z, rScale.x, rScale.y, rScale.z);
    }

    void setMtxAxisXYZ(MtxPtr pMtx, const Vec3f& rAxisX, const Vec3f& rAxisY, const Vec3f& rAxisZ) {
        pMtx[0][0] = rAxisX.x;
        pMtx[1][0] = rAxisX.y;
        pMtx[2][0] = rAxisX.z;
        pMtx[0][1] = rAxisY.x;
        pMtx[1][1] = rAxisY.y;
        pMtx[2][1] = rAxisY.z;
        pMtx[0][2] = rAxisZ.x;
        pMtx[1][2] = rAxisZ.y;
        pMtx[2][2] = rAxisZ.z;
    }

    void flattenMtx(MtxPtr pDst, MtxPtr pSrc, const Vec3f& rNormal) {
        Vec3f axisX, axisY, axisZ;
        MR::extractMtxXDir(pSrc, &axisX);
        MR::extractMtxYDir(pSrc, &axisY);
        MR::extractMtxZDir(pSrc, &axisZ);

        axisX.orthogonalize(rNormal);
        axisY.orthogonalize(rNormal);
        axisZ.orthogonalize(rNormal);

        MR::setMtxAxisXYZ(pDst, axisX, axisY, axisZ);
    }

    void flattenMtx(MtxPtr pMtx, const Vec3f& rNormal) {
        flattenMtx(pMtx, pMtx, rNormal);
    }

    void blendMtxRotate(MtxPtr pMtxA, MtxPtr pMtxB, f32 blend, MtxPtr pDst) {
        Quaternion quatA, quatB, quatR;
        NebulaQuatFromMtx(&quatA, pMtxA);
        NebulaQuatFromMtx(&quatB, pMtxB);
        NebulaQuatLerp(&quatA, &quatB, blend, &quatR);
        NebulaMtxQuat(pDst, &quatR);
    }

    void blendMtxRotateSlerp(MtxPtr pMtxA, MtxPtr pMtxB, f32 blend, MtxPtr pDst) {
        Quaternion quatA, quatB, quatR;
        NebulaQuatFromMtx(&quatA, pMtxA);
        NebulaQuatFromMtx(&quatB, pMtxB);
        NebulaQuatSlerp(&quatA, &quatB, &quatR, blend);
        NebulaMtxQuat(pDst, &quatR);
    }

    void blendMtx(MtxPtr pMtxA, MtxPtr pMtxB, f32 blend, MtxPtr pDst) {
        Vec3f transA, transB, transR;
        extractMtxTrans(pMtxA, &transA);
        extractMtxTrans(pMtxB, &transB);

        transR = transA * (1.0f - blend) + transB * blend;

        blendMtxRotate(pMtxA, pMtxB, blend, pDst);
        setMtxTrans(pDst, transR.x, transR.y, transR.z);
    }

    void makeMtxWithoutScale(Pos3f* pDst, const Pos3f& rSrc) {
        Vec3f trans;
        rSrc.getTrans(trans);

        Vec3f axisX, axisY, axisZ;
        rSrc.getXYZDir(axisX, axisY, axisZ);

        pDst->setTrans(trans);

        MR::normalize(&axisX);
        MR::normalize(&axisY);
        MR::normalize(&axisZ);

        pDst->setXYZDir(axisX, axisY, axisZ);
    }

    void makeRTFromMtxPtr(Vec3f* pOutTrans, Vec3f* pOutRot, MtxPtr pSrc, bool toDegree) {
        const Pos3f mtx(pSrc);
        if (pOutTrans != nullptr) {
            mtx.getTrans(*pOutTrans);
        }

        if (pOutRot != nullptr) {
            mtx.getEuler(*pOutRot);

            if (toDegree) {
                pOutRot->set(*pOutRot * _180_PI);
            }
        }
    }

    void makeMtxSideUp(Pos3f* pDst, const Vec3f& rSide, const Vec3f& rUp) {
        Vec3f axisZ, axisY, axisX;
        MR::normalize(rSide, &axisX);

        MR::crossOrPerpendicular(&axisZ, axisX, rUp);
        MR::normalize(&axisZ);

        axisY.cross(axisZ, axisX);

        pDst->setXYZDir(axisX, axisY, axisZ);
    }

    void makeMtxSideUpPos(Pos3f* pDst, const Vec3f& rSide, const Vec3f& rUp, const Vec3f& rPos) {
        makeMtxSideUp(pDst, rSide, rUp);
        pDst->setTrans(rPos);
    }

    void makeMtxSideFront(Pos3f* pDst, const Vec3f& rSide, const Vec3f& rFront) {
        Vec3f axisZ, axisY, axisX;
        MR::normalize(rSide, &axisX);

        MR::crossOrPerpendicular(&axisY, rFront, axisX);
        MR::normalize(&axisY);

        axisZ.cross(axisX, axisY);

        pDst->setXYZDir(axisX, axisY, axisZ);
    }

    void makeMtxUpSide(MtxPtr pDst, const Vec3f& rUp, const Vec3f& rSide) {
        Vec3f axisZ, axisY, axisX;
        MR::normalize(rUp, &axisY);

        MR::crossOrPerpendicular(&axisZ, rSide, axisY);
        MR::normalize(&axisZ);

        axisX.cross(axisY, axisZ);

        setMtxAxisXYZ(pDst, axisX, axisY, axisZ);
    }

    void makeMtxUpSide(Pos3f* pDst, const Vec3f& rUp, const Vec3f& rSide) {
        makeMtxUpSide(pDst->toMtxPtr(), rUp, rSide);
    }

    void makeMtxUpSidePos(Pos3f* pDst, const Vec3f& rUp, const Vec3f& rSide, const Vec3f& rPos) {
        makeMtxUpSide(pDst, rUp, rSide);
        pDst->setTrans(rPos);
    }

    void makeMtxUpFront(MtxPtr pDst, const Vec3f& rUp, const Vec3f& rFront) {
        Vec3f axisZ, axisY, axisX;
        MR::normalize(rUp, &axisY);

        MR::crossOrPerpendicular(&axisX, axisY, rFront);
        MR::normalize(&axisX);

        axisZ.cross(axisX, axisY);

        setMtxAxisXYZ(pDst, axisX, axisY, axisZ);
    }

    void makeMtxUpFront(Pos3f* pDst, const Vec3f& rUp, const Vec3f& rFront) {
        makeMtxUpFront(pDst->toMtxPtr(), rUp, rFront);
    }

    void makeMtxUpFrontPos(Pos3f* pDst, const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rPos) {
        makeMtxUpFront(pDst, rUp, rFront);
        pDst->setTrans(rPos);
    }

    void makeMtxFrontSidePos(Pos3f* pDst, const Vec3f& rFront, const Vec3f& rSide, const Vec3f& rPos) {
        Vec3f axisX, axisY, axisZ;
        MR::normalize(rFront, &axisZ);

        MR::crossOrPerpendicular(&axisY, axisZ, rSide);
        MR::normalize(&axisY);

        axisX.cross(axisY, axisZ);

        pDst->setXYZDir(axisX, axisY, axisZ);
        pDst->setTrans(rPos);
    }

    void makeMtxFrontUp(MtxPtr pDst, const Vec3f& rFront, const Vec3f& rUp) {
        Vec3f axisZ, axisY, axisX;
        MR::normalize(rFront, &axisZ);

        MR::crossOrPerpendicular(&axisX, rUp, axisZ);
        MR::normalize(&axisX);

        axisY.cross(axisZ, axisX);

        setMtxAxisXYZ(pDst, axisX, axisY, axisZ);
    }

    void makeMtxFrontUp(Pos3f* pDst, const Vec3f& rFront, const Vec3f& rUp) {
        makeMtxFrontUp(pDst->toMtxPtr(), rFront, rUp);
    }

    void makeMtxFrontUpPos(Pos3f* pDst, const Vec3f& rFront, const Vec3f& rUp, const Vec3f& rPos) {
        makeMtxFrontUp(pDst, rFront, rUp);
        pDst->setTrans(rPos);
    }

    void makeMtxUpNoSupport(Pos3f* pDst, const Vec3f& rUp) {
        Vec3f support;
        if (MR::getMaxAbsElementIndex(rUp) == 2) {
            support.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            support.set< f32 >(0.0f, 0.0f, 1.0f);
        }

        Vec3f axisX, axisY, axisZ;
        MR::normalize(rUp, &axisY);

        axisX.cross(axisY, support);
        MR::normalize(&axisX);

        axisZ.cross(axisX, axisY);

        pDst->setXYZDir(axisX, axisY, axisZ);
    }

    void makeMtxUpNoSupportPos(Pos3f* pDst, const Vec3f& rUp, const Vec3f& rPos) {
        Vec3f support;
        if (MR::getMaxAbsElementIndex(rUp) == 2) {
            support.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            support.set< f32 >(0.0f, 0.0f, 1.0f);
        }

        MR::makeMtxUpFrontPos(pDst, rUp, support, rPos);
    }

    void makeMtxFrontNoSupport(Pos3f* pDst, const Vec3f& rFront) {
        Vec3f support;
        if (MR::getMaxAbsElementIndex(rFront) == 1) {
            support.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            support.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        Vec3f axisX, axisY, axisZ;
        MR::normalize(rFront, &axisZ);

        axisX.cross(support, axisZ);
        MR::normalize(&axisX);

        axisY.cross(axisZ, axisX);

        pDst->setXYZDir(axisX, axisY, axisZ);
    }

    void makeMtxFrontNoSupportPos(Pos3f* pDst, const Vec3f& rFront, const Vec3f& rPos) {
        Vec3f support;
        if (MR::getMaxAbsElementIndex(rFront) == 1) {
            support.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            support.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        MR::makeMtxFrontUpPos(pDst, rFront, support, rPos);
    }

    void orthogonalize(Pos3f* pMtx) {
        Vec3f axisZ, axisY, axisX;

        pMtx->getXYZDir(axisX, axisY, axisZ);

        Vec3f* pZ;
        Vec3f* pY;
        pY = &axisY;
        pZ = &axisZ;

        NebulaVecCrossProduct(pY, pZ, &axisX);
        NebulaVecCrossProduct(pZ, &axisX, pY);

        axisX.normalize();
        pY->normalize();

        pMtx->setXYZDir(axisX, *pY, *pZ);

        Vec3f zDir;
        pMtx->getZDir(zDir);

        FloatUtil::sqrt((pMtx->get(0, 0) * pMtx->get(0, 0)) + (pMtx->get(1, 0) * pMtx->get(1, 0)) + (pMtx->get(2, 0) * pMtx->get(2, 0)) +
                                      (pMtx->get(0, 1) * pMtx->get(0, 1)) + (pMtx->get(1, 1) * pMtx->get(1, 1)) + (pMtx->get(2, 1) * pMtx->get(2, 1)) +
                                      (pMtx->get(0, 2) * pMtx->get(0, 2)) + (pMtx->get(1, 2) * pMtx->get(1, 2)) + (pMtx->get(2, 2) * pMtx->get(2, 2)));

        if (pMtx != nullptr) {
            f32 invLenX = FloatUtil::inv_sqrt((pMtx->get(0, 0) * pMtx->get(0, 0)) + (pMtx->get(1, 0) * pMtx->get(1, 0)) +
                                                            (pMtx->get(2, 0) * pMtx->get(2, 0)));
            pMtx->mMtx[0][0] = invLenX * pMtx->get(0, 0);
            pMtx->mMtx[1][0] = invLenX * pMtx->get(1, 0);
            pMtx->mMtx[2][0] = invLenX * pMtx->get(2, 0);

            f32 invLenY = FloatUtil::inv_sqrt((pMtx->get(0, 1) * pMtx->get(0, 1)) + (pMtx->get(1, 1) * pMtx->get(1, 1)) +
                                                            (pMtx->get(2, 1) * pMtx->get(2, 1)));
            pMtx->mMtx[0][1] = invLenY * pMtx->get(0, 1);
            pMtx->mMtx[1][1] = invLenY * pMtx->get(1, 1);
            pMtx->mMtx[2][1] = invLenY * pMtx->get(2, 1);

            f32 invLenZ = FloatUtil::inv_sqrt((pMtx->get(0, 2) * pMtx->get(0, 2)) + (pMtx->get(1, 2) * pMtx->get(1, 2)) +
                                                            (pMtx->get(2, 2) * pMtx->get(2, 2)));
            pMtx->mMtx[0][2] = invLenZ * pMtx->get(0, 2);
            pMtx->mMtx[1][2] = invLenZ * pMtx->get(1, 2);
            pMtx->mMtx[2][2] = invLenZ * pMtx->get(2, 2);
        }

        pMtx->mMtx[0][2] = zDir.x;
        pMtx->mMtx[1][2] = zDir.y;
        pMtx->mMtx[2][2] = zDir.z;
    }

    void turnMtxToXDirRate(Pos3f* pMtx, const Vec3f& rDir, f32 rate) {
        Quat4f quat;
        pMtx->getQuat(quat);
        MR::turnQuatXDirRate(&quat, quat, rDir, rate);
        pMtx->setQuat(quat);
    }

    void turnMtxToXDirDegree(Pos3f* pMtx, const Vec3f& rDir, f32 degree) {
        Quat4f quat;
        pMtx->getQuat(quat);
        MR::turnQuatXDirRad(&quat, quat, rDir, PI_180 * degree);
        pMtx->setQuat(quat);
    }

    void turnMtxToYDirRate(Pos3f* pMtx, const Vec3f& rDir, f32 rate) {
        Quat4f quat;
        pMtx->getQuat(quat);
        MR::turnQuatYDirRate(&quat, quat, rDir, rate);
        pMtx->setQuat(quat);
    }

    void turnMtxToYDirDegree(Pos3f* pMtx, const Vec3f& rDir, f32 degree) {
        Quat4f quat;
        pMtx->getQuat(quat);
        MR::turnQuatYDirRad(&quat, quat, rDir, PI_180 * degree);
        pMtx->setQuat(quat);
    }

    void turnMtxToLocalDirDegree(Pos3f* pMtx, const Vec3f& rLocalAxis, const Vec3f& rDir, f32 degree) {
        Vec3f worldAxis;
        pMtx->mult33(rLocalAxis, worldAxis);

        Quat4f quat;
        pMtx->getQuat(quat);
        MR::turnQuat(&quat, quat, worldAxis, rDir, PI_180 * degree);
        pMtx->setQuat(quat);
    }

    void makeMtxMoment(Pos3f* pMtx, const Vec3f& rMoment) {
        pMtx->identity();

        if (!rMoment.isZero()) {
            Vec3f dir(rMoment);
            f32 mag;
            MR::separateScalarAndDirection(&mag, &dir, dir);
            pMtx->makeRotate(dir, mag);
        }
    }

    void rotateMtxMoment(Pos3f* pDst, const Pos3f& rSrc, const Vec3f& rMoment) {
        if (rMoment.isZero()) {
            *pDst = rSrc;
        } else {
            Vec3f dir(rMoment);
            f32 mag;
            MR::separateScalarAndDirection(&mag, &dir, dir);

            Vec3f trans;
            rSrc.getTrans(trans);

            Pos3f rotMtx;
            rotMtx.makeRotate(dir, mag);
            rotMtx.concat(rotMtx, rSrc);
            orthogonalize(&rotMtx);
            rotMtx.setTrans(trans);
            *pDst = rotMtx;
        }
    }

    void rotateMtxMoment(Pos3f* pMtx, const Vec3f& rMoment) {
        rotateMtxMoment(pMtx, *pMtx, rMoment);
    }

    void setMtxTrans(MtxPtr pMtx, f32 x, f32 y, f32 z) {
        pMtx[0][3] = x;
        pMtx[1][3] = y;
        pMtx[2][3] = z;
    }

    void addTransMtx(MtxPtr pMtx, const Vec3f& rVec) {
        pMtx[0][3] += rVec.x;
        pMtx[1][3] += rVec.y;
        pMtx[2][3] += rVec.z;
    }

    void addTransMtxLocal(MtxPtr pMtx, const Vec3f& rVec) {
        addTransMtxLocalX(pMtx, rVec.x);
        addTransMtxLocalY(pMtx, rVec.y);
        addTransMtxLocalZ(pMtx, rVec.z);
    }

    void addTransMtxLocalX(MtxPtr pMtx, f32 x_coord) {
        pMtx[0][3] = pMtx[0][3] + (pMtx[0][0] * x_coord);
        pMtx[1][3] = pMtx[1][3] + (pMtx[1][0] * x_coord);
        pMtx[2][3] = pMtx[2][3] + (pMtx[2][0] * x_coord);
    }

    void addTransMtxLocalY(MtxPtr pMtx, f32 y_coord) {
        pMtx[0][3] = pMtx[0][3] + (pMtx[0][1] * y_coord);
        pMtx[1][3] = pMtx[1][3] + (pMtx[1][1] * y_coord);
        pMtx[2][3] = pMtx[2][3] + (pMtx[2][1] * y_coord);
    }

    void addTransMtxLocalZ(MtxPtr pMtx, f32 z_coord) {
        pMtx[0][3] = pMtx[0][3] + (pMtx[0][2] * z_coord);
        pMtx[1][3] = pMtx[1][3] + (pMtx[1][2] * z_coord);
        pMtx[2][3] = pMtx[2][3] + (pMtx[2][2] * z_coord);
    }

    void extractMtxXDir(MtxPtr pMtx, Vec3f* pOut) {
        pOut->x = pMtx[0][0];
        pOut->y = pMtx[1][0];
        pOut->z = pMtx[2][0];
    }

    void extractMtxYDir(MtxPtr pMtx, Vec3f* pOut) {
        pOut->x = pMtx[0][1];
        pOut->y = pMtx[1][1];
        pOut->z = pMtx[2][1];
    }

    void extractMtxZDir(MtxPtr pMtx, Vec3f* pOut) {
        pOut->x = pMtx[0][2];
        pOut->y = pMtx[1][2];
        pOut->z = pMtx[2][2];
    }

    void extractMtxXYZDir(MtxPtr pMtx, Vec3f* pOutX, Vec3f* pOutY, Vec3f* pOutZ) {
        MR::extractMtxXDir(pMtx, pOutX);
        MR::extractMtxYDir(pMtx, pOutY);
        MR::extractMtxZDir(pMtx, pOutZ);
    }

    void extractMtxTrans(MtxPtr pMtx, Vec3f* pOut) {
        pOut->x = pMtx[0][3];
        pOut->y = pMtx[1][3];
        pOut->z = pMtx[2][3];
    }

    bool isSameMtx(MtxPtr pMtxA, MtxPtr pMtxB) {
        for (s32 row = 0; row < 3; row++) {
            for (s32 column = 0; column < 4; column++) {
                if (pMtxA[row][column] != pMtxB[row][column]) {
                    return false;
                }
            }
        }

        return true;
    }

    // The translation column is not compared.
    bool isSameMtxRot(MtxPtr pMtxA, MtxPtr pMtxB) {
        for (s32 row = 0; row < 3; row++) {
            for (s32 column = 0; column < 3; column++) {
                if (pMtxA[row][column] != pMtxB[row][column]) {
                    return false;
                }
            }
        }

        return true;
    }

    bool isRotAxisY(MtxPtr pMtxA, MtxPtr pMtxB) {
        Vec3f yDirA, yDirB;
        extractMtxYDir(pMtxA, &yDirA);
        extractMtxYDir(pMtxB, &yDirB);
        return FloatUtil::epsilonEquals(yDirA.x, yDirB.x, FloatUtil::epsilon()) &&
               FloatUtil::epsilonEquals(yDirA.y, yDirB.y, FloatUtil::epsilon()) &&
               FloatUtil::epsilonEquals(yDirA.z, yDirB.z, FloatUtil::epsilon());
    }

    void calcMtxRotAxis(Vec3f* pOut, MtxPtr pMtxA, MtxPtr pMtxB) {
        Vec3f localY(0.0f, 1.0f, 0.0f);

        Mtx invA;
        NebulaMtxInverse(pMtxA, invA);

        Vec3f axisYA, axisYB;
        NebulaMtxMultVecSR(invA, &localY, &axisYA);
        NebulaMtxMultVecSR(pMtxB, &axisYA, &axisYB);

        Vec3f cross = localY.cross(axisYB);

        if (MR::normalizeOrZero(&cross)) {
            *pOut = localY;
        } else {
            *pOut = cross;
        }
    }

    MtxPtr tmpMtxTrans(const Vec3f& rVec) {
        setMtxTrans(mtrans_org, rVec.x, rVec.y, rVec.z);
        return mtrans_org;
    }

    MtxPtr tmpMtxScale(f32 sx, f32 sy, f32 sz) {
        tmpmtx_sc[0][0] = sx;
        tmpmtx_sc[1][1] = sy;
        tmpmtx_sc[2][2] = sz;
        return tmpmtx_sc;
    }

    MtxPtr tmpMtxRotXRad(f32 rad) {
        f32 cosX = Angle::cosRadian(rad);
        f32 sinX = Angle::sinRadian(rad);
        tmpmtx_rx[1][1] = cosX;
        tmpmtx_rx[2][1] = sinX;
        tmpmtx_rx[1][2] = -sinX;
        tmpmtx_rx[2][2] = cosX;
        return tmpmtx_rx;
    }

    MtxPtr tmpMtxRotYRad(f32 rad) {
        f32 cosY = Angle::cosRadian(rad);
        f32 sinY = Angle::sinRadian(rad);
        tmpmtx_ry[0][0] = cosY;
        tmpmtx_ry[0][2] = sinY;
        tmpmtx_ry[2][0] = -sinY;
        tmpmtx_ry[2][2] = cosY;
        return tmpmtx_ry;
    }

    MtxPtr tmpMtxRotZRad(f32 rad) {
        f32 cosZ = Angle::cosRadian(rad);
        f32 sinZ = Angle::sinRadian(rad);
        tmpmtx_rz[0][0] = cosZ;
        tmpmtx_rz[1][0] = sinZ;
        tmpmtx_rz[0][1] = -sinZ;
        tmpmtx_rz[1][1] = cosZ;
        return tmpmtx_rz;
    }

    MtxPtr tmpMtxRotXDeg(f32 deg) {
        f32 cosX = Angle::cosDegree(deg);
        f32 sinX = Angle::sinDegree(deg);
        tmpmtx_rx[1][1] = cosX;
        tmpmtx_rx[2][1] = sinX;
        tmpmtx_rx[1][2] = -sinX;
        tmpmtx_rx[2][2] = cosX;
        return tmpmtx_rx;
    }

    MtxPtr tmpMtxRotYDeg(f32 deg) {
        f32 cosY = Angle::cosDegree(deg);
        f32 sinY = Angle::sinDegree(deg);
        tmpmtx_ry[0][0] = cosY;
        tmpmtx_ry[0][2] = sinY;
        tmpmtx_ry[2][0] = -sinY;
        tmpmtx_ry[2][2] = cosY;
        return tmpmtx_ry;
    }

    MtxPtr tmpMtxRotZDeg(f32 deg) {
        f32 cosZ = Angle::cosDegree(deg);
        f32 sinZ = Angle::sinDegree(deg);
        tmpmtx_rz[0][0] = cosZ;
        tmpmtx_rz[1][0] = sinZ;
        tmpmtx_rz[0][1] = -sinZ;
        tmpmtx_rz[1][1] = cosZ;
        return tmpmtx_rz;
    }

    void orderRotateMtx(s16 order, const Vec3f& rRad, MtxPtr pDst) {
        const Vec3f* pRad = &rRad;
        MtxPtr mtxX = tmpMtxRotXRad(pRad->x);
        MtxPtr mtxY = tmpMtxRotYRad(pRad->y);
        MtxPtr mtxZ = tmpMtxRotZRad(pRad->z);

        // Order 0 is Y, X, Z; order 5 is Y, Z, X.
        MR::multMtx(pDst, order == 5 ? mtxZ : mtxX, mtxY);
        MR::multMtx(pDst, pDst, order == 5 ? mtxX : mtxZ);
    }

    void rotAxisVecRad(const Vec3f& rVec, const Vec3f& rAxis, Vec3f* pOut, f32 rad) {
        Mtx rot;
        NebulaMtxRotAxisRad(rot, &rAxis, rad);
        NebulaMtxMultVec(rot, &rVec, pOut);
    }
};  // namespace MR

