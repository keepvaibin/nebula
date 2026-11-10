#include "Game/AreaObj/CameraRepulsiveArea.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getRepulsion_f1, "getRepulsion.f1", 2.0f);

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

    f32 base = (side.length() * cNum_getRepulsion_f1) / MR::getCylinderRadius(this);

    f32 denom = base;

    for (int i = 0; i < 2; i++) {
        denom *= base;
    }

    ret /= denom;

    return ret;
}
