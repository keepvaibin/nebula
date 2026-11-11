#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/System/LumaCore.hpp"

SunshadeMapHolder::SunshadeMapHolder() : NameObj("日よけコリジョン管理") {
    lumacore::setClass(this, lumacore::kClassId< SunshadeMapHolder >);
}

namespace MR {
    // Shade: an occluder on the vertical line from rPos up to rPos + sunHeight.
    bool isInShadeFromTheSun(const Vec3f& rPos, f32 sunHeight) {
        if (!MR::isInAreaObj("SunLightArea", rPos)) {
            return true;
        }

        if (!MR::isExistSceneObj(SceneObj_SunshadeMapHolder)) {
            return false;
        }

        return Collision::checkStrikeLineToSunshade(rPos, Vec3f(0.0f, sunHeight, 0.0f), 0, nullptr, nullptr);
    }

    void createSunshadeMapHolder() {
        MR::createSceneObj(SceneObj_SunshadeMapHolder);
    }
};  // namespace MR
