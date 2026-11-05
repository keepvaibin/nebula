#include <nebula_geometry.hpp>

void dummy(const Quat4f& rRotation, Vec3f& rVector) {
    void (Quat4f::*rotate)(Vec3f&) const = &Quat4f::rotate;
    (rRotation.*rotate)(rVector);
}
