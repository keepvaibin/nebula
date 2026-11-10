#include "Game/Boss/TombSpiderSensorCtrl.hpp"
#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderFunction.hpp"
#include "Game/Boss/TombSpiderGland.hpp"
#include "Game/Boss/TombSpiderVitalSpot.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateAttackSensor_f1, "updateAttackSensor.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f5, "initSensor.f5", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f6, "initSensor.f6", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f7, "initSensor.f7", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f8, "initSensor.f8", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f9, "initSensor.f9", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f10, "initSensor.f10", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f11, "initSensor.f11", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f12, "initSensor.f12", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f13, "initSensor.f13", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f14, "initSensor.f14", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f15, "initSensor.f15", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f16, "initSensor.f16", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f17, "initSensor.f17", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f18, "initSensor.f18", 750.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f19, "initSensor.f19", 50.0f);

namespace {
    NEBULA_TUNABLE(f32, sAttackSensorDistanceOffset, 100.0f);
    NEBULA_TUNABLE(f32, sEyeSensorPosOffset, -100.0f);
    NEBULA_TUNABLE(f32, sHipSensorPosOffset, -400.0f);
};  // namespace

TombSpiderSensorCtrl::TombSpiderSensorCtrl(TombSpider* pOwner) : mParent(pOwner) {
}

// The fight is side-on: every sensor sits at the player's depth.
namespace {
    void setSensorPosAlongZ(HitSensor* pSensor, MtxPtr pMtx, f32 offset) {
        pSensor->mPosition.x = offset * pMtx[0][2] + pMtx[0][3];
        pSensor->mPosition.y = offset * pMtx[1][2] + pMtx[1][3];
        pSensor->mPosition.z = MR::getPlayerPos()->z;
    }

    void setSensorPosAtPart(HitSensor* pSensor, const LiveActor* pPart) {
        pSensor->mPosition.set< f32 >(pPart->mPosition.x, pPart->mPosition.y, MR::getPlayerPos()->z);
    }
};  // namespace

void TombSpiderSensorCtrl::update(HitSensor* pSensor) {
    if (updateMainParts(pSensor) || updateGland(pSensor) || updateVitalSpot(pSensor)) {
        return;
    }

    updateAttackSensor(pSensor);
}

bool TombSpiderSensorCtrl::updateMainParts(HitSensor* pSensor) {
    if (pSensor->isType(ATYPE_TOMB_SPIDER_EYE)) {
        ::setSensorPosAlongZ(pSensor, TombSpiderFunction::getMtxEye(mParent), ::sEyeSensorPosOffset);
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_HIP)) {
        ::setSensorPosAlongZ(pSensor, TombSpiderFunction::getMtxHip(mParent), ::sHipSensorPosOffset);
        return true;
    }

    return false;
}

bool TombSpiderSensorCtrl::updateGland(HitSensor* pSensor) {
    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_FRONT_L)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getGlandFrontL(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_FRONT_R)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getGlandFrontR(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_REAR_L)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getGlandRearL(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_REAR_R)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getGlandRearR(mParent));
        return true;
    }

    return false;
}

bool TombSpiderSensorCtrl::updateVitalSpot(HitSensor* pSensor) {
    if (pSensor->isType(ATYPE_TOMB_SPIDER_VITAL_SPOT_C)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getVitalSpotC(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_VITAL_SPOT_L)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getVitalSpotL(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_VITAL_SPOT_R)) {
        ::setSensorPosAtPart(pSensor, TombSpiderFunction::getVitalSpotR(mParent));
        return true;
    }

    return false;
}

namespace {
    void updateAttackSensorPos(HitSensor* pSensor, const LiveActor* pActor) {
        MtxPtr baseMtx = pActor->getBaseMtx();
        Vec2f pos(baseMtx[0][2], baseMtx[1][2]);
        MR::normalize(&pos);
        f32 dist = MR::calcDistanceXY(pActor->mPosition, *MR::getPlayerPos()) - ::sAttackSensorDistanceOffset;
        pos.scale(dist);
        pos.x += pActor->mPosition.x;
        pos.y += pActor->mPosition.y;
        pSensor->mPosition.set< f32 >(pos.x, pos.y, MR::getPlayerPos()->z);
    }
};  // namespace

bool TombSpiderSensorCtrl::updateAttackSensor(HitSensor* pSensor) {
    if (pSensor->isType(ATYPE_ENEMY_ATTACK)) {
        f32 dist = MR::calcDistanceXY(mParent->mPosition, *MR::getPlayerPos()) - ::sAttackSensorDistanceOffset;

        Vec2f pos(1.0f, 0.0f);

        MR::rotateVecDegree(&pos, mParent->getRotate() - cNum_updateAttackSensor_f1);
        pos.scale(dist);
        pos.x += mParent->mPosition.x;
        pos.y += mParent->mPosition.y;
        pSensor->mPosition.set< f32 >(pos.x, pos.y, MR::getPlayerPos()->z);
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_FRONT_L_ATTACKER)) {
        ::updateAttackSensorPos(pSensor, TombSpiderFunction::getGlandFrontL(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_FRONT_R_ATTACKER)) {
        ::updateAttackSensorPos(pSensor, TombSpiderFunction::getGlandFrontR(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_REAR_L_ATTACKER)) {
        ::updateAttackSensorPos(pSensor, TombSpiderFunction::getGlandRearL(mParent));
        return true;
    }

    if (pSensor->isType(ATYPE_TOMB_SPIDER_GLAND_REAR_R_ATTACKER)) {
        ::updateAttackSensorPos(pSensor, TombSpiderFunction::getGlandRearR(mParent));
        return true;
    }

    return false;
}

void TombSpiderSensorCtrl::initSensor() {
    MR::addHitSensor(mParent, "body", ATYPE_TOMB_SPIDER_BODY, 8, cNum_initSensor_f1, Vec3f(0.0f, -cNum_initSensor_f2, 0.0f));

    MR::addHitSensorCallback(mParent, "eye", ATYPE_TOMB_SPIDER_EYE, 8, cNum_initSensor_f3);
    MR::addHitSensorCallback(mParent, "hip", ATYPE_TOMB_SPIDER_HIP, 8, cNum_initSensor_f4);
    MR::addHitSensorCallback(mParent, "mouth", ATYPE_TOMB_SPIDER_MOUTH, 8, cNum_initSensor_f5);
    MR::addHitSensorCallback(mParent, "gland_front_l", ATYPE_TOMB_SPIDER_GLAND_FRONT_L, 8, cNum_initSensor_f6);
    MR::addHitSensorCallback(mParent, "gland_front_r", ATYPE_TOMB_SPIDER_GLAND_FRONT_R, 8, cNum_initSensor_f7);
    MR::addHitSensorCallback(mParent, "gland_rear_l", ATYPE_TOMB_SPIDER_GLAND_REAR_L, 8, cNum_initSensor_f8);
    MR::addHitSensorCallback(mParent, "gland_rear_r", ATYPE_TOMB_SPIDER_GLAND_REAR_R, 8, cNum_initSensor_f9);
    MR::addHitSensorCallback(mParent, "attacker", ATYPE_ENEMY_ATTACK, 8, cNum_initSensor_f10);
    MR::addHitSensorCallback(mParent, "front_l_attacker", ATYPE_TOMB_SPIDER_GLAND_FRONT_L_ATTACKER, 8, cNum_initSensor_f11);
    MR::addHitSensorCallback(mParent, "front_r_attacker", ATYPE_TOMB_SPIDER_GLAND_FRONT_R_ATTACKER, 8, cNum_initSensor_f12);
    MR::addHitSensorCallback(mParent, "rear_l_attacker", ATYPE_TOMB_SPIDER_GLAND_REAR_L_ATTACKER, 8, cNum_initSensor_f13);
    MR::addHitSensorCallback(mParent, "rear_r_attacker", ATYPE_TOMB_SPIDER_GLAND_REAR_R_ATTACKER, 8, cNum_initSensor_f14);
    MR::addHitSensorCallback(mParent, "vital_spot_c", ATYPE_TOMB_SPIDER_VITAL_SPOT_C, 8, cNum_initSensor_f15);
    MR::addHitSensorCallback(mParent, "vital_spot_l", ATYPE_TOMB_SPIDER_VITAL_SPOT_L, 8, cNum_initSensor_f16);
    MR::addHitSensorCallback(mParent, "vital_spot_r", ATYPE_TOMB_SPIDER_VITAL_SPOT_R, 8, cNum_initSensor_f17);

    MR::addHitSensor(mParent, "cocoon", ATYPE_TOMB_SPIDER_COCOON, 8, cNum_initSensor_f18, Vec3f(0.0f, -cNum_initSensor_f19, 0.0f));

    TombSpiderFunction::invalidateAllPartsSensor(mParent);
}
