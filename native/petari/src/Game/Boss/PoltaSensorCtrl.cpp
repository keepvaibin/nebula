#include "Game/Boss/PoltaSensorCtrl.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaArm.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaArena.hpp"

namespace {
    const PoltaSensorListEntry sCoreSensorList[] = {
        {"Core", "Tongue3", 150.0f, 0.0f, 0.0f, 0.0f},
    };

    const PoltaSensorListEntry sBodySensorList[] = {{
                                                        "Body1",
                                                        "Head1",
                                                        320.0f,
                                                        100.0f,
                                                        -20.0f,
                                                        0.0f,
                                                    },
                                                    {
                                                        "Body2",
                                                        "Head2",
                                                        260.0f,
                                                        180.0f,
                                                        0.0f,
                                                        0.0f,
                                                    },
                                                    {
                                                        "Body3",
                                                        "Head3",
                                                        290.0f,
                                                        320.0f,
                                                        0.0f,
                                                        0.0f,
                                                    }};

    const PoltaSensorListEntry sLeftArmSensorList[] = {
        {
            "ArmL1",
            "ArmL1",
            140.0f,
            90.0f,
            0.0f,
            0.0f,
        },
        {
            "ArmL2",
            "ArmL2",
            140.0f,
            120.0f,
            0.0f,
            0.0f,
        },
        {
            "HandL",
            "HandL",
            240.0f,
            220.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLA1",
            "FingerLA1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLA2",
            "FingerLA2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLB1",
            "FingerLB1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLB2",
            "FingerLB2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLC1",
            "FingerLC1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLC2",
            "FingerLC2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLD1",
            "FingerLD1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerLD2",
            "FingerLD2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
    };

    const PoltaSensorListEntry sRightArmSensorList[] = {
        {
            "ArmR1",
            "ArmR1",
            140.0f,
            90.0f,
            0.0f,
            0.0f,
        },
        {
            "ArmR2",
            "ArmR2",
            140.0f,
            120.0f,
            0.0f,
            0.0f,
        },
        {
            "HandR",
            "HandR",
            240.0f,
            220.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRA1",
            "FingerRA1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRA2",
            "FingerRA2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRB1",
            "FingerRB1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRB2",
            "FingerRB2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRC1",
            "FingerRC1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRC2",
            "FingerRC2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRD1",
            "FingerRD1",
            140.0f,
            130.0f,
            0.0f,
            0.0f,
        },
        {
            "FingerRD2",
            "FingerRD2",
            120.0f,
            140.0f,
            0.0f,
            0.0f,
        },
    };
};  // namespace

static_assert(ARRAY_SIZE(::sCoreSensorList) == PoltaSensorCtrl::cCoreSensorNum, "core table size");
static_assert(ARRAY_SIZE(::sBodySensorList) == PoltaSensorCtrl::cBodySensorNum, "body table size");
static_assert(ARRAY_SIZE(::sLeftArmSensorList) == PoltaSensorCtrl::cArmSensorNum, "left arm table size");
static_assert(ARRAY_SIZE(::sRightArmSensorList) == PoltaSensorCtrl::cArmSensorNum, "right arm table size");

namespace {
    Vec3f getOffset(const PoltaSensorListEntry& rEntry) {
        return Vec3f(rEntry.mOffsetX, rEntry.mOffsetY, rEntry.mOffsetZ);
    }

    template < s32 N >
    void addJointSensors(HitSensor* (&rSensors)[N], Polta* pPolta, const PoltaSensorListEntry (&rList)[N]) {
        for (s32 i = 0; i < N; i++) {
            rSensors[i] = MR::addHitSensorAtJointEnemy(pPolta, rList[i].mName, rList[i].mJointName, 8, rList[i].mRadius, getOffset(rList[i]));
        }
    }

    // Arm sensors follow the arm actor's joints.
    template < s32 N >
    void addArmSensors(HitSensor* (&rSensors)[N], Polta* pPolta, PoltaArm* pArm, const PoltaSensorListEntry (&rList)[N]) {
        for (s32 i = 0; i < N; i++) {
            rSensors[i] =
                MR::addHitSensorMtxEnemy(pPolta, rList[i].mName, 8, rList[i].mRadius, MR::getJointMtx(pArm, rList[i].mJointName), getOffset(rList[i]));
        }
    }

    template < s32 N >
    bool contains(HitSensor* const (&rSensors)[N], const HitSensor* pSensor) {
        for (s32 i = 0; i < N; i++) {
            if (rSensors[i] == pSensor) {
                return true;
            }
        }

        return false;
    }
}  // namespace

PoltaSensorCtrl::PoltaSensorCtrl(Polta* pPolta)
    : mPoltaPtr(pPolta), mScreamSensor(nullptr), mHeadSensor(nullptr), mCoreSensors(), mBodySensors(), mLeftArmSensors(), mRightArmSensors() {
}

void PoltaSensorCtrl::setScreamSensorSize(f32 screamSensorSize) {
    if (mScreamSensor != nullptr) {
        mScreamSensor->mRadius = screamSensorSize;
    }
}

void PoltaSensorCtrl::initSensor() {
    ::addJointSensors(mCoreSensors, mPoltaPtr, ::sCoreSensorList);
    ::addJointSensors(mBodySensors, mPoltaPtr, ::sBodySensorList);
    ::addArmSensors(mLeftArmSensors, mPoltaPtr, mPoltaPtr->mLeftArm, ::sLeftArmSensorList);
    ::addArmSensors(mRightArmSensors, mPoltaPtr, mPoltaPtr->mRightArm, ::sRightArmSensorList);

    mScreamSensor = MR::addHitSensorAtJointEnemy(mPoltaPtr, "Scream", "Tongue3", 64, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    mHeadSensor = MR::addHitSensorAtJointEnemy(mPoltaPtr, "Head", "Head5", 32, 200.0f, Vec3f(-50.0f, 0.0f, 0.0f));
}

bool PoltaSensorCtrl::isCoreSensor(const HitSensor* pSensor) const {
    return ::contains(mCoreSensors, pSensor);
}

bool PoltaSensorCtrl::isBodySensor(const HitSensor* pSensor) const {
    return ::contains(mBodySensors, pSensor);
}

bool PoltaSensorCtrl::isLeftArmSensor(const HitSensor* pSensor) const {
    return ::contains(mLeftArmSensors, pSensor);
}

bool PoltaSensorCtrl::isRightArmSensor(const HitSensor* pSensor) const {
    return ::contains(mRightArmSensors, pSensor);
}

bool PoltaSensorCtrl::isHeadSensor(const HitSensor* pSensor) const {
    return mHeadSensor == pSensor;
}

bool PoltaSensorCtrl::isScreamSensor(const HitSensor* pSensor) const {
    return mScreamSensor == pSensor;
}
