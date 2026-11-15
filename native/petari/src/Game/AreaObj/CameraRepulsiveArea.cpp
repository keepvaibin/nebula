#include "Game/AreaObj/CameraRepulsiveArea.hpp"
#include "Game/Util/AreaObjUtil.hpp"

Vec3f CameraRepulsiveSphere::getRepulsion(const Vec3f& /*rRep*/) {
    return Vec3f(0.0f, 0.0f, 0.0f);
}

Vec3f CameraRepulsiveCylinder::getRepulsion(const Vec3f& rRep) {
    Vec3f position;
    MR::calcCylinderPos(&position, this);
    Vec3f upVec;
    MR::calcCylinderUpVec(&upVec, this);

    Vec3f ret(rRep);
    ret -= position;
    f32 dot = upVec.dot(ret);
    Vec3f side;
    Vec3f along(upVec);
    along *= dot;
    ret -= along;

    side.cross(ret, upVec);

    f32 base = (side.length() * 2.0f) / MR::getCylinderRadius(this);

    f32 denom = base;

    for (int i = 0; i < 2; i++) {
        denom *= base;
    }

    ret /= denom;

    return ret;
}
