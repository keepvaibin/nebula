#include "Game/Boss/DodoryuUtil.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

namespace DodoryuUtil {
    bool calcVerticalizedDir(Dodoryu* pDodoryu, Vec3f* pVec, const Vec3f& rVec) {
        Vec3f yDir;
        pDodoryu->mBaseMtx.getYDir(yDir);

        pVec->killElement(rVec, yDir);

        if (MR::isNearZero(*pVec)) {
            return false;
        }

        MR::normalize(pVec);

        return true;
    }

    void turnToward(Dodoryu* pDodoryu, const Vec3f& rDirection, f32 maxAngle) {
        Vec3f up;
        Vec3f front;
        Vec3f position;
        pDodoryu->mBaseMtx.getYDir(up);
        pDodoryu->mBaseMtx.getZDir(front);
        pDodoryu->mBaseMtx.getTrans(position);

        f32 maxRadian = maxAngle * MR::pi() / 180.0f;

        Pos3f mtx;
        mtx.makeRotate(front, rDirection, maxRadian);
        mtx.concat(pDodoryu->mBaseMtx);
        mtx.setTrans(position);
        pDodoryu->setMtx(mtx);
    }

    void accelerate(Dodoryu* pDodoryu, const Vec3f& rDirection, f32 acceleration, f32 lateralDamping, f32 maxSpeed) {
        Vec3f direction(rDirection);
        Vec3f& rVelocity = pDodoryu->mVelocity;
        Vec3f lateralVelocity;
        lateralVelocity.killElement(rVelocity, direction);
        rVelocity -= lateralVelocity;
        lateralVelocity *= lateralDamping;
        rVelocity += lateralVelocity;
        rVelocity += direction * acceleration;

        if (rVelocity.length() > maxSpeed) {
            rVelocity.setLength(maxSpeed);
        }

        Vec3f yDir;
        pDodoryu->mBaseMtx.getYDir(yDir);
        rVelocity.orthogonalize(yDir);
    }

    void addVelocity(Dodoryu* pDodoryu, bool isSnap) {
        Vec3f trans;
        Pos3f mtx = pDodoryu->mBaseMtx;

        mtx.getTrans(trans);
        trans.add(pDodoryu->mVelocity);
        mtx.setTrans(trans);
        pDodoryu->setMtx(mtx);

        if (isSnap) {
            pDodoryu->snapToGround();
        }
    }

    void rotateVelocityByWall(Dodoryu* pDodoryu) {
        Vec3f up;
        pDodoryu->mBaseMtx.getYDir(up);

        f32 speed = pDodoryu->mVelocity.length();
        Vec3f velocity(pDodoryu->mVelocity);
        velocity.orthogonalize(pDodoryu->mWallNormal);

        if (MR::isNearZero(velocity)) {
            velocity.cross(up, pDodoryu->mWallNormal);
        }

        velocity.setLength(speed);
        pDodoryu->mVelocity.set(velocity);
    }

    void calcEscapeDir(Dodoryu* pDodoryu, Vec3f* pDirection, f32 maxAngle) {
        Vec3f up;
        const Vec3f* pPlayerPos = MR::getPlayerPos();
        Vec3f away(pDodoryu->mPosition);
        away.sub(*pPlayerPos);
        pDirection->set(away);
        if (!calcVerticalizedDir(pDodoryu, pDirection, *pDirection)) {
            pDodoryu->mBaseMtx.getZDir(*pDirection);
        }

        pDodoryu->mBaseMtx.getYDir(up);
        const f32 randomAngle = 2.0f * maxAngle * (MR::getRandom() - 0.5f);
        f32 angle = randomAngle * MR::pi() / 180.0f;
        Pos3f rotation;
        rotation.makeRotate(up, angle);
        rotation.mult33(*pDirection);
    }
};  // namespace DodoryuUtil
