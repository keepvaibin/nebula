#include "Game/Map/SunshadeMapParts.hpp"
#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

SunshadeMapParts::SunshadeMapParts(const char* pName) : GeneralMapParts(pName), mCollision(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< SunshadeMapParts >);
}

void SunshadeMapParts::calcAnim() {
    LiveActor::calcAnim();

    if (mCollision != nullptr) {
        MR::setCollisionMtx(this, mCollision);
    }
}

namespace MR {
    NameObj* createSunshadeMapParts(const char* pName) {
        return new SunshadeMapParts(pName);
    }
};  // namespace MR

void SunshadeMapParts::init(const TableIter& rIter) {
    GeneralMapParts::init(rIter);
    MR::createSunshadeMapHolder();
    mCollision = MR::tryCreateCollisionSunshade(this, getSensor("body"));
}
