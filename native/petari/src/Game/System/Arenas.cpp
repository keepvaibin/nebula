#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    NebulaArena* sPermanent = nullptr;
    NebulaArena* sGame = nullptr;
    NebulaArena* sSceneFiles = nullptr;
    NebulaArena* sScene = nullptr;

    void destroyArena(NebulaArena** ppArena) {
        if (*ppArena != nullptr) {
            NebulaArena::destroy(*ppArena);
            *ppArena = nullptr;
        }
    }
}  // namespace

namespace Arenas {
    void create() {
        sPermanent = NebulaArena::create(NebulaArena::root());
        sGame = NebulaArena::create(NebulaArena::root());
    }

    NebulaArena* permanent() {
        return sPermanent;
    }

    NebulaArena* game() {
        return sGame;
    }

    NebulaArena* sceneFiles() {
        return sSceneFiles;
    }

    NebulaArena* scene() {
        return sScene;
    }

    void createSceneFiles() {
        sSceneFiles = NebulaArena::create(sGame);
    }

    void createScene() {
        sScene = NebulaArena::create(sGame);
    }

    void destroyScene() {
        destroyArena(&sScene);
    }

    void resetGame() {
        destroyArena(&sScene);
        destroyArena(&sSceneFiles);
        destroyArena(&sGame);
        sGame = NebulaArena::create(NebulaArena::root());
    }
}  // namespace Arenas
