#pragma once

#include "Game/Boss/PoltaRock.hpp"
#include <nebula_geometry.hpp>

class Polta;
class PoltaArm;
class HitSensor;

namespace PoltaFunction {
    void onMovement(Polta* pPolta);
    PoltaArm* getLeftArmActor(Polta* pPolta);
    PoltaArm* getRightArmActor(Polta* pPolta);
    bool isCoreSensor(const Polta* pPolta, const HitSensor* pSensor);
    bool isBodySensor(const Polta* pPolta, const HitSensor* pSensor);
    bool isArmSensor(const Polta* pPolta, const HitSensor* pSensor);
    bool isLeftArmSensor(const Polta* pPolta, const HitSensor* pSensor);
    bool isRightArmSensor(const Polta* pPolta, const HitSensor* pSensor);
    bool isHeadSensor(const Polta* pPolta, const HitSensor* pSensor);
    void setScreamSensorSize(Polta* pPolta, f32 screamSensorSize);
    bool attackScreamSensor(Polta* pPolta, HitSensor* pSender, HitSensor* pReceiver);
    void startAction(Polta* pPolta, const char* pActionName, bool isAffectBody);
    void startBckBody(Polta* pPolta, const char* pActionName);
    void setBodyHP(Polta* pPolta, s32 hp);
    void damageLeftArm(Polta* pPolta);
    void damageRightArm(Polta* pPolta);
    void requestStartControllArm(Polta* pPolta);
    void startArm(Polta* pPolta);
    void breakLeftArm(Polta* pPolta);
    void breakRightArm(Polta* pPolta);
    void killLeftArm(Polta* pPolta);
    void killRightArm(Polta* pPolta);
    bool isBrokenLeftArm(Polta* pPolta);
    bool isBrokenRightArm(Polta* pPolta);
    bool isEnableAttackRightArm(Polta* pPolta);
    bool isEnableAttackLeftArm(Polta* pPolta);
    bool isMaxGenerateBombTeresa(Polta* pPolta);
    s32 getCountDeadGroundRock(Polta* pPolta);
    bool appearGroundRock(Polta* pPolta, f32 distance, f32 angle);
    bool appearRockCircle(Polta* pPolta, const Vec3f& rCenter, f32 radius, s32 index, PoltaRock::Color color);
    bool appearBombTeresaFromRoot(Polta* pPolta);
    bool appearBombTeresaNormal(Polta* pPolta, const Vec3f& rPosition, const Vec3f& rVelocity);
    void disperseBombTeresa(Polta* pPolta);
    void breakGroundRock(Polta* pPolta);
    void killBombTeresa(Polta* pPolta);
    void killPoltaRock(Polta* pPolta);
    void killGroundRock(Polta* pPolta);
};  // namespace PoltaFunction
