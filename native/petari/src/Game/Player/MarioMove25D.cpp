#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_update25Dmode_f1, "update25Dmode.f1", 0.965f);
NEBULA_TUNABLE_NAMED(f32, cNum_update25Dmode_f2, "update25Dmode.f2", 0.9397f);
NEBULA_TUNABLE_NAMED(f32, cNum_update25Dmode_f3, "update25Dmode.f3", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_update25Dmode_f4, "update25Dmode.f4", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateAxisFromMode_f1, "updateAxisFromMode.f1", 0.99f);

void Mario::set25Dmode(const AreaObj* pArea) {
    Mtx matrix;
    Vec3f rotation;
    MR::calcCubeRotate(pArea, &rotation);
    Vec3f normal(0.0f, 0.0f, 1.0f);
    Vec3f up(0.0f, 1.0f, 0.0f);
    MR::makeMtxTR(matrix, 0.0f, 0.0f, 0.0f, rotation.x, rotation.y, rotation.z);
    NebulaMtxMultVec(matrix, &normal, &normal);
    NebulaMtxMultVec(matrix, &up, &up);
    m25DNormal = normal;
    m25DUp = up;
    m25DAcross.cross(m25DNormal, m25DUp);
    MR::normalize(&m25DAcross);
    mMovementStates.mode25D = true;
}

void Mario::update25Dmode() {
    Vec3f up;
    MR::vecKillElement(-*getGravityVec(), m25DNormal, &up);
    MR::normalizeOrZero(&up);
    if (MR::isNearZero(up)) {
        m25DMode = -getGravityVec()->dot(m25DNormal) > 0.0f ? 4 : 5;
        return;
    }

    f32 sideDot = up.dot(m25DAcross);
    f32 upDot = up.dot(m25DUp);
    f32 normalDot = up.dot(m25DNormal);
    f32 sideAbs = MR::abs(sideDot);
    f32 upAbs = MR::abs(upDot);
    f32 normalAbs = MR::abs(normalDot);
    u8 mode;
    if (sideAbs > upAbs && sideAbs > normalAbs) {
        mode = sideDot > 0.0f ? 2 : 3;
    } else if (upAbs > sideAbs && upAbs > normalAbs) {
        if (upDot > 0.0f) {
            mode = 0;
        } else {
            mode = 1;
        }
    } else {
        mode = normalDot > 0.0f ? 4 : 5;
    }

    if (mode == 2 || mode == 3) {
        if (mode == 2 || mode == 3) {
            f32 threshold = cNum_update25Dmode_f1;
            if (m25DMode == 2 || m25DMode == 3) {
                threshold = cNum_update25Dmode_f2;
            }

            if (sideAbs < threshold) {
                if (upAbs > normalAbs) {
                    if (upDot > 0.0f) {
                        mode = 0;
                    } else {
                        mode = 1;
                    }
                } else {
                    return;
                }
            }
        }
    }

    if (!isStickOn()) {
        m25DMode = mode;
        mMovementStates.stick25DLatched = false;
    } else if (!mMovementStates.stick25DLatched && mStickPos.z > cNum_update25Dmode_f3) {
        m25DStick.set(mStickPos.x, mStickPos.y);
        m25DMode = mode;
        mMovementStates.stick25DLatched = true;
    } else {
        Vec2f stick(mStickPos.x, mStickPos.y);
        if (MR::diffAngleAbs(m25DStick, stick) >= cNum_update25Dmode_f4) {
            m25DMode = mode;
            m25DStick.set(mStickPos.x, mStickPos.y);
        }
    }
}

void Mario::updateAxisFromMode(u8 mode) {
    if (mMovementStates.jumping) {
        return;
    }

    Vec3f side;
    Vec3f normal;
    if (mode < 4) {
        Vec3f up;
        Vec3f planeUp;
        MR::vecKillElement(-*getGravityVec(), m25DNormal, &up);
        MR::vecKillElement(m25DUp, m25DNormal, &planeUp);
        MR::normalizeOrZero(&up);
        MR::normalizeOrZero(&planeUp);
        f32 dot = up.dot(planeUp);
        if (MR::abs(dot) > cNum_updateAxisFromMode_f1) {
            switch (mode) {
            case 0:
                side = m25DAcross;
                normal = m25DNormal;
                if (dot < 0.0f) {
                    side = -side;
                }

                break;
            case 1:
                side = m25DAcross;
                normal = m25DNormal;
                if (dot > 0.0f) {
                    side = -side;
                }

                break;
            case 2:
                side = m25DNormal;
                normal = -m25DAcross;
                if (dot < 0.0f) {
                    side = -side;
                    normal = -normal;
                }

                break;
            case 3:
                side = -m25DNormal;
                normal = m25DAcross;
                if (dot < 0.0f) {
                    side = -side;
                    normal = -normal;
                }

                break;
            }
        } else {
            Vec3f axis;
            axis.cross(up, planeUp);
            MR::normalize(&axis);
            if (axis.dot(m25DNormal) < 0.0f) {
                axis = -axis;
            }

            Vec3f tangent;
            tangent.cross(axis, up);
            MR::normalize(&tangent);
            switch (mode) {
            case 0:
                side = tangent;
                normal = axis;
                break;
            case 1:
                side = -tangent;
                normal = axis;
                break;
            case 2:
                side = axis;
                normal = -tangent;
                break;
            case 3:
                side = -axis;
                normal = tangent;
                break;
            }
        }
    }

    m25DSideVec = side;
    m25DDownVec = -normal;
}

void Mario::calcMoveDir25D(f32 x, f32 y, Vec3f* pDirection) {
    *pDirection = m25DSideVec * x - m25DDownVec * y;
}
