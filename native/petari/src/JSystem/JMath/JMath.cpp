#include "JSystem/JMath/JMath.hpp"
#include "JSystem/JMath/JMATrigonometric.hpp"

void JMAEulerToQuat(s16 x, s16 y, s16 z, Quaternion* quat) {
    f32 cosX, cosY, cosZ, sinX, sinY, sinZ;
    JMASSinCos(x / 2, &sinX, &cosX);
    JMASSinCos(y / 2, &sinY, &cosY);
    JMASSinCos(z / 2, &sinZ, &cosZ);

    f32 cyz = cosY * cosZ;
    f32 syz = sinY * sinZ;
    quat->w = cosX * (cyz) + sinX * (syz);
    quat->x = sinX * (cyz)-cosX * (syz);
    quat->y = cosZ * (cosX * sinY) + sinZ * (sinX * cosY);
    quat->z = sinZ * (cosX * cosY) - cosZ * (sinX * sinY);
}
