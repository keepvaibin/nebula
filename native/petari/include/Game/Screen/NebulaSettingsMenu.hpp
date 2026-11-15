#pragma once

// Nebula's settings menu in the game's own look: the quit question's white window over a dimmed screen, the game's
// buttons and message font. Its pages and rows are the host's (NebulaHostServices::settings_view: the settings, the
// draft and its apply), named in the played language. It drops in over anything (F1, View+Menu, Minus+Plus, or the
// pause menu's Settings) and holds the whole game still as the HOME Menu does, its sound and movies with it, until it
// has risen away again.
namespace NebulaSettingsMenu {
    void init();             // GameSystem::initAfterSystemArchiveLoaded
    void update();           // GameSystem::update, once a frame
    void draw();             // GameSystem::draw, over the scene and its wipes
    bool isFreezingScene();  // open or opening or closing: the scene waits
    bool isShown();          // drawn this frame (the pointer is drawn over it)
    void requestOpen();      // the pause menu's Settings
    // A host text (English capitals, as the host's rows give it) in the played language as the menu shows it, into pDst
    // (at most `capacity` units with the terminator); its length. The mod log's host sentences (LumaModLog).
    s32 translateHost(char16_t* pDst, s32 capacity, const char* pText);
};  // namespace NebulaSettingsMenu
