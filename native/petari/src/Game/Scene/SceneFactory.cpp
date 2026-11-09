#include "Game/Scene/SceneFactory.hpp"
#include "Game/Scene/BootScene.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/IntermissionScene.hpp"

namespace MR {
    Scene* createScene(SceneKind kind) {
        switch (kind) {
        case SceneKind_Game:
            return new GameScene();
        case SceneKind_Intermission:
            return new IntermissionScene();
        case SceneKind_Boot:
            return new BootScene();
        case SceneKind_None:
            break;
        }
        return nullptr;
    }

    const char* getSceneKindName(SceneKind kind) {
        switch (kind) {
        case SceneKind_Game:
            return "Game";
        case SceneKind_Intermission:
            return "Intermission";
        case SceneKind_Boot:
            return "Boot";
        case SceneKind_None:
            break;
        }
        return "";
    }
};  // namespace MR
