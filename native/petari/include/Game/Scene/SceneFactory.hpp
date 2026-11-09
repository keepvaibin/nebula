#pragma once

#include <nebula_types.h>

class Scene;

// The scenes the system controller makes.
enum SceneKind : u8 {
    SceneKind_None,
    SceneKind_Boot,
    SceneKind_Game,
    SceneKind_Intermission,
};

namespace MR {
    Scene* createScene(SceneKind kind);
    // For the controller's state text.
    const char* getSceneKindName(SceneKind kind);
};  // namespace MR
