#pragma once

#ifdef TARGET_PC
// Nebula: whether DLSS Frame Generation may add frames, as NVIDIA's guide asks: only while the game shows play.
// Over a menu, a window or a full-screen page it has no scene to pace (the paused game draws no live 3D scene, so
// its HUD-less picture is missing) and its generated frames bend the screen's still panels. The game reports each
// frame; the host takes a report it has not had for a while as "no" (loading, other scenes).
namespace NebulaFrameGen {
    // Each frame a full-screen menu, page or system window is up (its control or calcAnim): generation pauses.
    void noteFullscreenUi();
    // GameScene::draw, each frame: `playing` when the scene shows play (not paused, no map, no staff roll, no movie).
    void report(bool playing);
};  // namespace NebulaFrameGen
#endif
