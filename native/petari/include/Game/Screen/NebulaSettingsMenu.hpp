#pragma once

// Nebula's settings menu in the game's own look: the quit question's white window over a dimmed screen, the game's
// buttons and message font. Its pages and rows are the host's (NebulaHostServices::settings_view: the settings, the
// draft and its apply), named in the played language. It drops in over anything (F1, View+Menu, Minus+Plus, or the
// pause menu's Settings) and holds the whole game still as the HOME Menu does, its sound and movies with it, until it
// has risen away again. The HDR calibration's reference stage (NebulaSettingsView::scene_live) plays under it instead,
// given no controls, its sound held as ever.
namespace NebulaSettingsMenu {
    void init();             // GameSystem::initAfterSystemArchiveLoaded
    void update();           // GameSystem::update, once a frame
    void draw();             // GameSystem::draw, over the scene and its wipes
    bool isUp();             // open or opening or closing
    bool isFreezingScene();  // up, and the scene waits (it plays under the menu while isSceneLive)
    bool isSceneLive();      // the HDR calibration's reference stage plays under the menu
    bool isShown();          // drawn this frame (the pointer is drawn over it)
    void requestOpen();      // the pause menu's Settings
    // A host text (English capitals, as the host's rows give it) in the played language as the menu shows it, into pDst
    // (at most `capacity` units with the terminator); its length. The mod log's host sentences (LumaModLog).
    s32 translateHost(char16_t* pDst, s32 capacity, const char* pText);
};  // namespace NebulaSettingsMenu

// The benchmark's run panel at the screen's top left while a run measures (NebulaHostServices::benchmark_state): the
// stage, the frame rate, the step, the run's progress and how to stop it.
namespace NebulaBenchmarkPanel {
    void init();    // GameSystem::initAfterSystemArchiveLoaded
    void update();  // GameSystem::update, once a frame
    void draw();    // GameSystem::draw, over the game's screens
};  // namespace NebulaBenchmarkPanel
