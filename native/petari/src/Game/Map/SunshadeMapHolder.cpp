#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MapUtil.hpp"

SunshadeMapHolder::SunshadeMapHolder() : NameObj("\223\372\202\346\202\257\203\122\203\212\203\127\203\207\203\223\212\307\227\235" /* 日よけコリジョン管理 */), _C(0.0f, 1.0f, 0.0f) {
}

namespace MR {
    bool isInShadeFromTheSun(const TVec3f& rPos, f32 a2) {
        if (!MR::isInAreaObj("SunLightArea", rPos)) {
            return true;
        }

        if (!MR::isExistSceneObj(SceneObj_SunshadeMapHolder)) {
            return false;
        }

        SunshadeMapHolder* holder = MR::getSceneObj< SunshadeMapHolder >(SceneObj_SunshadeMapHolder);

        return Collision::checkStrikeLineToSunshade(rPos, holder->_C * a2, 0, nullptr, nullptr);
    }

    void createSunshadeMapHolder() {
        MR::createSceneObj(SceneObj_SunshadeMapHolder);
    }
};  // namespace MR
