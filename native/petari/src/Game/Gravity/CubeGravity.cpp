#include "Game/Gravity.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

// A point's region around the cube: base-3 digits per axis (below, between, above the box).
namespace {
    enum Region {
        Region_NegativeX = 0,
        Region_BetweenX = 1,
        Region_PositiveX = 2,
        Region_NegativeY = 0,
        Region_BetweenY = 3,
        Region_PositiveY = 6,
        Region_NegativeZ = 0,
        Region_BetweenZ = 9,
        Region_PositiveZ = 18,
    };

    // An edge region: the axis it runs along stands in for that axis's "between" digit.
    enum Axis {
        Axis_X = 1,
        Axis_Y = 3,
        Axis_Z = 9,
    };

    constexpr s32 encodeRegion(s32 x, s32 y, s32 z) {
        return x + y + z;
    }

    constexpr s32 encodeEdge(s32 axis, s32 half1, s32 half2) {
        return axis + half1 + half2;
    }

    // Signs -1, 0 or 1 per axis.
    constexpr s32 encodeCorner(s32 signumX, s32 signumY, s32 signumZ) {
        return (signumX + 1) + (signumY + 1) * 3 + (signumZ + 1) * 9;
    }

    const s32 cRegionXFacePositive = encodeRegion(Region_PositiveX, Region_BetweenY, Region_BetweenZ);
    const s32 cRegionXFaceNegative = encodeRegion(Region_NegativeX, Region_BetweenY, Region_BetweenZ);
    const s32 cRegionYFacePositive = encodeRegion(Region_BetweenX, Region_PositiveY, Region_BetweenZ);
    const s32 cRegionYFaceNegative = encodeRegion(Region_BetweenX, Region_NegativeY, Region_BetweenZ);
    const s32 cRegionZFacePositive = encodeRegion(Region_BetweenX, Region_BetweenY, Region_PositiveZ);
    const s32 cRegionZFaceNegative = encodeRegion(Region_BetweenX, Region_BetweenY, Region_NegativeZ);
}  // namespace

CubeGravity::CubeGravity() : PlanetGravity() {
    lenX = 1.0;
    lenY = 1.0;
    lenZ = 1.0;
    mActiveFaces = Face_All;

    mCube.identity();
    mPosition.identity();
}

void CubeGravity::setCube(const Pos3f& rCube) {
    mCube = rCube;

    updateIdentityMtx();
}

int CubeGravity::calcGravityArea(const Vec3f& rPosition) const {
    Vec3f dirX, dirY, dirZ, trans;
    mPosition.getXDir(dirX);
    mPosition.getYDir(dirY);
    mPosition.getZDir(dirZ);
    mPosition.getTrans(trans);
    Vec3f relativePosition = rPosition - trans;
    int area;  // Region of the cube
    f32 xDirDistance = relativePosition.dot(dirX) / lenX, yDirDistance = relativePosition.dot(dirY) / lenY,
        zDirDistance = relativePosition.dot(dirZ) / lenZ;

    if (xDirDistance < -lenX) {
        if ((mActiveFaces & Face_NegativeX) == 0) {
            return -1;
        }
        area = Region_NegativeX;
    } else {
        if (xDirDistance <= lenX) {
            area = Region_BetweenX;
        } else {
            if ((mActiveFaces & Face_PositiveX) == 0) {
                return -1;
            }
            area = Region_PositiveX;
        }
    }

    if (yDirDistance < -lenY) {
        if ((mActiveFaces & Face_NegativeY) == 0) {
            return -1;
        }
        area += Region_NegativeY;
    } else {
        if (yDirDistance <= lenY) {
            area += Region_BetweenY;
        } else {
            if ((mActiveFaces & Face_PositiveY) == 0) {
                return -1;
            }
            area += Region_PositiveY;
        }
    }

    if (zDirDistance < -lenZ) {
        if ((mActiveFaces & Face_NegativeZ) == 0) {
            return -1;
        }
        area += Region_NegativeZ;
    } else {
        if (zDirDistance <= lenZ) {
            area += Region_BetweenZ;
        } else {
            if ((mActiveFaces & Face_PositiveZ) == 0) {
                return -1;
            }
            area += Region_PositiveZ;
        }
    }

    return area;
}

bool CubeGravity::calcFaceGravity(const Vec3f& rPosition, s32 area, Vec3f* pDest, f32* pScalar) const {
    Vec3f antiFaceDir;  // Negative of the normal vector of the face an object is on
    switch (area) {
    case ::cRegionZFaceNegative:
        mPosition.getZDir(antiFaceDir);
        break;

    case ::cRegionYFaceNegative:
        mPosition.getYDir(antiFaceDir);
        break;

    case ::cRegionXFaceNegative:
        mPosition.getXDir(antiFaceDir);
        break;

    case ::cRegionXFacePositive:
        mPosition.getXDir(antiFaceDir);
        antiFaceDir.negate();
        break;

    case ::cRegionYFacePositive:
        mPosition.getYDir(antiFaceDir);
        antiFaceDir.negate();
        break;

    case ::cRegionZFacePositive:
        mPosition.getZDir(antiFaceDir);
        antiFaceDir.negate();
        break;

    default:
        return false;
    }
    Vec3f trans;
    f32 length;
    mPosition.getTrans(trans);
    MR::separateScalarAndDirection(&length, &antiFaceDir, antiFaceDir);
    f32 height = antiFaceDir.dot(trans - rPosition) - length;
    if (height < 0.0f) {
        height = 0.0f;
    }
    *pDest = antiFaceDir;
    *pScalar = height;
    return true;
}

bool CubeGravity::calcEdgeGravity(const Vec3f& rPosition, s32 area, Vec3f* pDest, f32* pScalar) const {
    // There is a mistake here: so long as area is not both even and negative, the function will not
    // return here. The intent is that area should be neither even nor negative, since all edges
    // are odd and positive. However, this mistake does not really matter since the switch will
    // return if this does not.
    if (!(((area & 1) ^ ((area >> 31) & 1)) - ((area >> 31) & 1)) ||
        area == ::encodeRegion(Region_BetweenX, Region_BetweenY, Region_BetweenZ)) {
        return false;
    }

    Vec3f edgeVector, edgeTranslation, xDir, yDir, zDir, trans, positionOppositeInOrthogonalPlane;

    mPosition.getXDir(xDir);
    mPosition.getYDir(yDir);
    mPosition.getZDir(zDir);

    switch (area) {
    case ::encodeEdge(Axis_X, Region_NegativeY, Region_NegativeZ):
        edgeVector = xDir;
        edgeTranslation = -yDir - zDir;
        break;

    case ::encodeEdge(Axis_Y, Region_NegativeX, Region_NegativeZ):
        edgeVector = yDir;
        edgeTranslation = -xDir - zDir;
        break;

    case ::encodeEdge(Axis_Y, Region_PositiveX, Region_NegativeZ):
        edgeVector = yDir;
        edgeTranslation = xDir - zDir;
        break;

    case ::encodeEdge(Axis_X, Region_PositiveY, Region_NegativeZ):
        edgeVector = xDir;
        edgeTranslation = yDir - zDir;
        break;

    case ::encodeEdge(Axis_Z, Region_NegativeX, Region_NegativeY):
        edgeVector = zDir;
        edgeTranslation = -xDir - yDir;
        break;

    case ::encodeEdge(Axis_Z, Region_PositiveX, Region_NegativeY):
        edgeVector = zDir;
        edgeTranslation = xDir - yDir;
        break;

    case ::encodeEdge(Axis_Z, Region_NegativeX, Region_PositiveY):
        edgeVector = zDir;
        edgeTranslation = -xDir + yDir;
        break;

    case ::encodeEdge(Axis_Z, Region_PositiveX, Region_PositiveY):
        edgeVector = zDir;
        edgeTranslation = xDir + yDir;
        break;

    case ::encodeEdge(Axis_X, Region_NegativeY, Region_PositiveZ):
        edgeVector = xDir;
        edgeTranslation = -yDir + zDir;
        break;

    case ::encodeEdge(Axis_Y, Region_NegativeX, Region_PositiveZ):
        edgeVector = yDir;
        edgeTranslation = -xDir + zDir;
        break;

    case ::encodeEdge(Axis_Y, Region_PositiveX, Region_PositiveZ):
        edgeVector = yDir;
        edgeTranslation = xDir + zDir;
        break;

    case ::encodeEdge(Axis_X, Region_PositiveY, Region_PositiveZ):
        edgeVector = xDir;
        edgeTranslation = yDir + zDir;
        break;

    default:
        return false;
    }

    mPosition.getTrans(trans);
    edgeTranslation += trans;

    MR::normalizeOrZero(&edgeVector);

    positionOppositeInOrthogonalPlane.killElement((edgeTranslation - rPosition), edgeVector);

    if (positionOppositeInOrthogonalPlane.isZero()) {
        pDest->normalize(edgeTranslation - trans);
        *pScalar = 0.0;
    } else {
        *pScalar = pDest->normalize(positionOppositeInOrthogonalPlane);
    }

    return true;
}

bool CubeGravity::calcCornerGravity(const Vec3f& rPosition, s32 area, Vec3f* pDest, f32* pScalar) const {
    Vec3f vertex, xDir, yDir, zDir, trans;

    mPosition.getXDir(xDir);
    mPosition.getYDir(yDir);
    mPosition.getZDir(zDir);

    switch (area) {
    case ::encodeCorner(-1, -1, -1):
        vertex = -xDir - yDir - zDir;
        break;

    case ::encodeCorner(1, -1, -1):
        vertex = xDir - yDir - zDir;
        break;

    case ::encodeCorner(-1, 1, -1):
        vertex = -xDir + yDir - zDir;
        break;

    case ::encodeCorner(1, 1, -1):
        vertex = xDir + yDir - zDir;
        break;

    case ::encodeCorner(-1, -1, 1):
        vertex = -xDir - yDir + zDir;
        break;

    case ::encodeCorner(1, -1, 1):
        vertex = xDir - yDir + zDir;
        break;

    case ::encodeCorner(-1, 1, 1):
        vertex = -xDir + yDir + zDir;
        break;

    case ::encodeCorner(1, 1, 1):
        vertex = xDir + yDir + zDir;
        break;

    default:
        return false;
    }

    mPosition.getTrans(trans);
    vertex += trans;

    Vec3f gravity = vertex - rPosition;
    if (gravity.isZero()) {
        *pScalar = 0.0;
        pDest->normalize(vertex - trans);
    } else {
        *pScalar = pDest->normalize(gravity);
    }

    return true;
}

bool CubeGravity::calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const {
    int area = calcGravityArea(rPosition);
    if (area < 0) {
        return false;
    }
    Vec3f gravityForce;
    f32 scalar;
    if (!calcFaceGravity(rPosition, area, &gravityForce, &scalar) && !calcEdgeGravity(rPosition, area, &gravityForce, &scalar) &&
        !calcCornerGravity(rPosition, area, &gravityForce, &scalar)) {
        return false;
    }

    if (!isInRangeDistance(scalar)) {
        return false;
    }

    if (pDest != nullptr) {
        *pDest = gravityForce;
    }

    if (pScalar != nullptr) {
        *pScalar = scalar;
    }
    return true;
}

void CubeGravity::updateMtx(const Pos3f& rMtx) {
    mPosition.concat(rMtx, mCube);
    Vec3f dir;
    mPosition.getXDir(dir);
    lenX = dir.length();
    mPosition.getYDir(dir);
    lenY = dir.length();
    mPosition.getZDir(dir);
    lenZ = dir.length();
}
