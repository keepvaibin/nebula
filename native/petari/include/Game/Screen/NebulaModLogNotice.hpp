#pragma once

// The mod log's notice (contract mod-log 4.1): each new Error said about a mod, one line at the screen's bottom left
// for 6 s, at most three at once; what was said before the first scene as one summary line. Made in a game scene's
// layouts only when the session has mods or something was said (luma::modlog::wanted); drawn under the pause screens
// and the settings menu. The lines being shown live past a scene change: the next scene's notice goes on with them.
namespace NebulaModLogNotice {
    void make();        // GameSceneLayoutHolder::init (every game scene: it forgets the last scene's)
    void update();      // GameScene::update, once a frame
    void draw();        // GameScene::draw2D, before the pause screens
};  // namespace NebulaModLogNotice
