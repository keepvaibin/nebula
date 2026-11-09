#include "Game/Scene/SceneFactory.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/IntermissionScene.hpp"
#include "Game/Scene/BootScene.hpp"
#include "Game/Util/StringUtil.hpp"
#include <algorithm>

namespace {
    struct Name2CreateFunc {
        bool operator==(const char* pName) const {
            return MR::isEqualString(mName, pName);
        }

        const char* mName;
        Scene* (*mCreateFunc)();
    };

    template < typename T >
    Scene* createScene() {
        return new T();
    }

    const Name2CreateFunc cCreateTable[] = {
        {"Game", createScene< GameScene >},
        {"Intermission", createScene< IntermissionScene >},
        {"Boot", createScene< BootScene >},
    };
};  // namespace

namespace MR {
    Scene* createScene(const char* pName) {
        const Name2CreateFunc* pIter = std::find(::cCreateTable, ::cCreateTable + ARRAY_SIZE(::cCreateTable), pName);

        if (pIter == &::cCreateTable[ARRAY_SIZE(::cCreateTable)]) {
            return nullptr;
        }

        return (*pIter->mCreateFunc)();
    }
};  // namespace MR
