#pragma once

#ifdef TARGET_PC
#include <revolution/types.h>

class LayoutActor;
namespace nw4r {
    namespace lyt {
        class TexMap;
    }
}

// The game's button icons (its picture font's A, B, C, Z, 1, 2, Plus and Minus) drawn as the player's controller's
// buttons: the host names the style (the controller in use, or the one chosen) and the button each stands for
// (NebulaHostServices::button_icons). Every text and layout that shows a button through the picture font shows them.
namespace NebulaIcons {
    // The stationed GDDR heap's room for our sheets, widths and code map (HeapMemoryWatcher); a font that needs more
    // keeps its own icons.
    const u32 cStationedBytes = 0x100000;
    // Once the stationed resources are loaded (the picture font among them), before the stationed heaps shrink to fit:
    // the font's sheets, widths and code map of ours.
    void init();
    // Each frame: the icons again when the host's choice changed.
    void update();
    // `pActor` (an IconAButton) drawn with the A button over people and signs as the icons have it: ours in place of
    // its own two pictures while it draws, its own after.
    void drawWithIcons(const LayoutActor* pActor);
    // A real Wii Remote is in the player's hand (the host's NebulaButtonIcons::wii_remote): the game's pictures of one
    // (how to shake it to spin, tilt it to roll the Star Ball or steer the Ray) show only then.
    bool wiiRemoteInUse();
    // The keyboard's keys in the picture font (the menu's keys): key NEBULA_KEY_* at cKeyCodeFirst + key, drawn when
    // asked for. keyCode: its code, 0 while the icons cannot be drawn (the menu names the key in words).
    const u16 cKeyCodeFirst = 0xE000;
    u16 keyCode(s32 key);
    // The Pair Joy-Con picture: each half (0 left, 1 right) in each look, textures of Nebula's own covering cJoyConBox
    // units of the owner's drawing (a half's body, cJoyConBody, in its middle). setJoyConDensity asks for them at
    // the screen's pixels per unit on each axis; paintJoyConAhead paints toward that for about `budgetMicros` (true
    // while there is more). joyConPicture is null until every look has been painted once; pictures painted for a new
    // density replace the shown ones once all of them are. joyConPictureSize: the shown pictures' size in units (the
    // box, a few texels wider: whole tiles), the body in their middle.
    enum { JoyConAway, JoyConShown, JoyConHeld, JoyConLooks };
    const f32 cJoyConBox[2] = {18.3f, 40.4f};
    const f32 cJoyConBody[2] = {13.3f, 37.1f};  // a half's body: the drawing's 12.07 wide, a little fuller
    const f32 cJoyConShoulder = 0.62f;           // its shoulder's band over the body's top
    const f32 cJoyConRail = 1.38f;               // its rail past the body's inner side
    const u32 cJoyConColours[2] = {0x00BBDDFF, 0xFF5F55FF};  // the drawing's blue (L) and red (R)
    void setJoyConDensity(f32 pixelsX, f32 pixelsY);
    bool paintJoyConAhead(u32 budgetMicros);
    nw4r::lyt::TexMap* joyConPicture(s32 half, s32 look);
    void joyConPictureSize(f32* pWidth, f32* pHeight);
};  // namespace NebulaIcons
#endif
