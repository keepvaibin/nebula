#pragma once

// A preparation in the game's own look: while the host prepares something before the title (NEBULA_OPTION_PREPARATION),
// the logo scene holds the title and this screen shows the progress, then shrinks and fades to black.
namespace NebulaPrepare {
    void init();       // GameSystem::initAfterSystemArchiveLoaded
    void update();     // GameSystem::update, once a frame
    void draw();       // GameSystem::draw, over the scene
    bool isHolding();  // BootScene: the title waits (the preparation runs, or its screen is still going away)
};  // namespace NebulaPrepare
