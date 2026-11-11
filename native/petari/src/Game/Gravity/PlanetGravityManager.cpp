#include "Game/Gravity.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // A starting capacity: registerGravity grows past it.
    NEBULA_TUNABLE(s32, sFirstCapacity, 128);
};  // namespace

PlanetGravityManager::PlanetGravityManager(const char* pName)
    : NameObj(pName), mGravities(new PlanetGravity*[::sFirstCapacity]), mMaxGravities(::sFirstCapacity), mNumGravities(0) {
    lumacore::setClass(this, lumacore::kClassId< PlanetGravityManager >);
}

bool PlanetGravityManager::calcTotalGravityVector(Vec3f* pGravity, GravityInfo* pInfo, const Vec3f& rPosition, u32 gravityType, const void* pHost) const {
    Vec3f totalGravity(0.0f, 0.0f, 0.0f);
    bool hasCalculated = false;

    if (pInfo != nullptr) {
        pInfo->init();
    }

    f32 largestScalar = -1.0f;
    s32 largestPriority = -1;

    for (s32 i = 0; i < mNumGravities; i++) {
        PlanetGravity* gravity = mGravities[i];

        if (!gravity->mActivated || !gravity->mValidFollower || !gravity->mAppeared || (gravityType & gravity->mGravityType) == 0 ||
            pHost == gravity->mHost) {
            continue;
        }

        // Sorted by priority, highest first: nothing past a lower one can apply.
        const s32 priority = gravity->mPriority;
        if (priority < largestPriority) {
            break;
        }

        Vec3f gravityVec;
        if (!gravity->calcGravity(&gravityVec, rPosition)) {
            continue;
        }

        const f32 scalar = gravityVec.length();
        bool storeInfo;
        if (priority == largestPriority) {
            totalGravity += gravityVec;
            storeInfo = largestScalar < scalar;
        } else {
            largestPriority = priority;
            totalGravity = gravityVec;
            storeInfo = true;
        }
        hasCalculated = true;

        if (pInfo != nullptr && storeInfo) {
            largestScalar = scalar;
            pInfo->mGravityVector = gravityVec;
            pInfo->mGravityInstance = gravity;
        }
    }

    if (pGravity != nullptr) {
        MR::normalizeOrZero(&totalGravity);
        *pGravity = totalGravity;
    }

    return hasCalculated;
}

void PlanetGravityManager::registerGravity(PlanetGravity* pGravity) {
    pGravity->mIsRegistered = true;
    if (mNumGravities >= mMaxGravities) {
        mGravities = MR::growArray(mGravities, mMaxGravities);
        mMaxGravities *= 2;
    }

    mGravities[mNumGravities++] = pGravity;

    sortGravities();
}

void PlanetGravityManager::sortGravities() {
    for (s32 i = mNumGravities - 1; i >= 1 && mGravities[i]->mPriority > mGravities[i - 1]->mPriority; i--) {
        bubbleDown(i);
    }
}

void PlanetGravityManager::bubbleDown(int i) {
    PlanetGravity* temp = mGravities[i - 1];
    mGravities[i - 1] = mGravities[i];
    mGravities[i] = temp;
}
