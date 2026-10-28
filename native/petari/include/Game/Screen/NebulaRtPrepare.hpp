#pragma once

#ifdef TARGET_PC
// Ray tracing's shader preparation in the game's own look: while the renderer's first ray tracing start compiles
// (NEBULA_OPTION_RT_PREPARATION), the logo scene holds the title and this screen shows the progress, then shrinks and
// fades to black.
namespace NebulaRtPrepare {
    void init();       // GameSystem, once the stationed layouts are loaded
    void update();     // GameSystem::update, once a frame
    void draw();       // GameSystem::draw, over the scene
    bool isHolding();  // LogoScene: the title waits (the preparation runs, or its screen is still going away)
};  // namespace NebulaRtPrepare
#endif
