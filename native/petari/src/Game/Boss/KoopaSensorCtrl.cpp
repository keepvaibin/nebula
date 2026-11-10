#include "Game/Boss/KoopaSensorCtrl.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f5, "initSensor.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f6, "initSensor.f6", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f7, "initSensor.f7", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f8, "initSensor.f8", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f9, "initSensor.f9", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f10, "initSensor.f10", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f11, "initSensor.f11", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f12, "initSensor.f12", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f13, "initSensor.f13", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f14, "initSensor.f14", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f15, "initSensor.f15", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f16, "initSensor.f16", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f17, "initSensor.f17", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f18, "initSensor.f18", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f19, "initSensor.f19", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f20, "initSensor.f20", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f21, "initSensor.f21", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f22, "initSensor.f22", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f23, "initSensor.f23", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f24, "initSensor.f24", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f25, "initSensor.f25", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f26, "initSensor.f26", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f27, "initSensor.f27", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f28, "initSensor.f28", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f29, "initSensor.f29", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f30, "initSensor.f30", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f31, "initSensor.f31", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f32, "initSensor.f32", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f33, "initSensor.f33", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f34, "initSensor.f34", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f35, "initSensor.f35", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f36, "initSensor.f36", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f37, "initSensor.f37", 260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f38, "initSensor.f38", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f39, "initSensor.f39", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f40, "initSensor.f40", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f41, "initSensor.f41", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f42, "initSensor.f42", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f43, "initSensor.f43", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f44, "initSensor.f44", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f45, "initSensor.f45", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f46, "initSensor.f46", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f47, "initSensor.f47", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f48, "initSensor.f48", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f49, "initSensor.f49", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f50, "initSensor.f50", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f51, "initSensor.f51", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f52, "initSensor.f52", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f53, "initSensor.f53", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f54, "initSensor.f54", 30.0f);

KoopaSensorCtrl::KoopaSensorCtrl(Koopa* pHost) : mHost(pHost) {
}

void KoopaSensorCtrl::initSensor() {
    MR::addHitSensorAtJoint(mHost, "Body", "Spine", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f1, Vec3f(-cNum_initSensor_f2, -cNum_initSensor_f3, 0.0f));
    MR::addHitSensorAtJoint(mHost, "Face", "Face", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f4, Vec3f(cNum_initSensor_f5, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "WristL", "WristL", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f6, Vec3f(cNum_initSensor_f7, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "WristR", "WristR", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f8, Vec3f(cNum_initSensor_f9, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "ArmL", "ArmL2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f10, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "ArmR", "ArmR2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f11, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "LegL", "LegL2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f12, Vec3f(cNum_initSensor_f13, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "LegR", "LegR2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f14, Vec3f(cNum_initSensor_f15, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "FootL", "FootL2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f16, Vec3f(-cNum_initSensor_f17, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "FootR", "FootR2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f18, Vec3f(-cNum_initSensor_f19, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "Tail", "Tail2", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f20, Vec3f(cNum_initSensor_f21, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "TailTop", "Tail3", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f22, Vec3f(cNum_initSensor_f23, 0.0f, 0.0f));

    MR::addHitSensorAtJointEnemy(mHost, "GuardBody", "Spine", 8, cNum_initSensor_f24, Vec3f(0.0f, -cNum_initSensor_f25, 0.0f));

    MR::invalidateHitSensor(mHost, "GuardBody");

    MR::addHitSensorAtJointEnemyAttack(mHost, "AttackShell", "Spine", 8, cNum_initSensor_f26, Vec3f(-cNum_initSensor_f27, cNum_initSensor_f28, 0.0f));
    MR::addHitSensorAtJointEnemyAttack(mHost, "AttackFootL", "FootL2", 8, cNum_initSensor_f29, Vec3f(-cNum_initSensor_f30, cNum_initSensor_f31, 0.0f));
    MR::addHitSensorAtJointEnemyAttack(mHost, "AttackFootR", "FootR2", 8, cNum_initSensor_f32, Vec3f(-cNum_initSensor_f33, cNum_initSensor_f34, 0.0f));

    MR::addHitSensor(mHost, "AttackHipDrop", ATYPE_KOOPA_ATTACK, 8, cNum_initSensor_f35, Vec3f(0.0f, cNum_initSensor_f36, 0.0f));

    MR::invalidateHitSensor(mHost, "AttackHipDrop");

    MR::addHitSensorAtJointEnemyAttack(mHost, "ChaseRollAttack", "Spine", 8, cNum_initSensor_f37, Vec3f(0.0f, 0.0f, 0.0f));

    MR::addHitSensorAtJoint(mHost, "ChaseRollFace", "Face", ATYPE_KOOPA_RECEIVER, 16, cNum_initSensor_f38, Vec3f(cNum_initSensor_f39, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "ChaseRollStarPiece", "Spine", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f40, Vec3f(0.0f, 0.0f, 0.0f));

    MR::invalidateHitSensor(mHost, "ChaseRollAttack");
    MR::invalidateHitSensor(mHost, "ChaseRollFace");
    MR::invalidateHitSensor(mHost, "ChaseRollStarPiece");

    MR::addHitSensorAtJointEnemyAttack(mHost, "AttackRoll", "Spine", 8, cNum_initSensor_f41, Vec3f(0.0f, 0.0f, 0.0f));

    MR::addHitSensorAtJoint(mHost, "AttackRollReceiver", "Spine", ATYPE_KOOPA_RECEIVER, 8, cNum_initSensor_f42, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJoint(mHost, "AttackRollStarPiece", "Spine", ATYPE_KOOPA_PUSH, 8, cNum_initSensor_f43, Vec3f(0.0f, 0.0f, 0.0f));

    MR::invalidateHitSensor(mHost, "AttackRoll");
    MR::invalidateHitSensor(mHost, "AttackRollReceiver");
    MR::invalidateHitSensor(mHost, "AttackRollStarPiece");

    MR::addHitSensorAtJoint(mHost, "ReceiverTail", "Tail2", ATYPE_KOOPA_RECEIVER, 8, cNum_initSensor_f44, Vec3f(cNum_initSensor_f45, cNum_initSensor_f46, 0.0f));
    MR::addHitSensorAtJoint(mHost, "ReceiverTailTop", "Tail3", ATYPE_KOOPA_RECEIVER, 8, cNum_initSensor_f47, Vec3f(cNum_initSensor_f48, cNum_initSensor_f49, 0.0f));
    MR::addHitSensorAtJoint(mHost, "DamageEscapeBody", "Spine", ATYPE_KOOPA_RECEIVER, 8, cNum_initSensor_f50, Vec3f(0.0f, cNum_initSensor_f51, 0.0f));
    MR::addHitSensorAtJoint(mHost, "DamageEscapePush", "Spine", ATYPE_KOOPA_ATTACK, 8, cNum_initSensor_f52, Vec3f(-cNum_initSensor_f53, -cNum_initSensor_f54, 0.0f));
}
