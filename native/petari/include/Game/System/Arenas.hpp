#pragma once

class NebulaArena;

// The game's arenas, by what each holds. The permanent and game arenas are made under the root at start; a scene's files
// and its objects are made in the game arena.
namespace Arenas {
    void create();

    // What lasts as long as the game: the sound system's, the files every scene shares, saves.
    NebulaArena* permanent();
    // What a game holds between resets: the two below live in it.
    NebulaArena* game();
    // The files a scene loads, kept while the next scene is the same scene and stage. Null before the first scene.
    NebulaArena* sceneFiles();
    // The scene's objects. Null between scenes.
    NebulaArena* scene();

    void createSceneFiles();
    void createScene();
    void destroyScene();
    // The scene's objects, then the files they use, then the game arena; a new game arena.
    void resetGame();
}  // namespace Arenas
