#pragma once

#include <Game/Ui/Pane.hpp>
#include <Game/Ui/Picture.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Window.hpp>
#include <Game/Ui/Font.hpp>
#include <Game/Util/Array.hpp>
#include <Game/Util/StringUtil.hpp>
#include <NebulaGpu.h>

class NebulaPaneKit;

// What Nebula's own screens (the achievement board, the settings menu) are built from: the game's message font placed
// by its letters (not its box), the game's windows reshaped and repainted, its buttons stretched to their labels. In
// screen units: the layout's units made square (readScreen).
namespace NebulaUi {
    typedef Ui::Pane Pane;
    typedef Ui::Picture Picture;
    typedef Ui::TextBox TextBox;
    typedef Ui::Window Window;

    // The game's message font (MessageFont26) is 24.3 wide for 33 high.
    const f32 cFontAspect = 24.3f / 33.0f;
    const f32 cCharSpace = 0.045f;  // of the height
    const f32 cLineSpace = 0.12f;

    // Where the message font's letters sit in their box, measured at 1080p: capitals stand 0.52 of the size tall;
    // centred in a box, their middle is 0.05 of the size above the box's middle; in a box aligned to its top they begin
    // 0.19 of the size below it and stand on a baseline 0.71 below it. The picture font's glyphs sit 0.04 of their size
    // below their box's middle. Every line of text here is placed by its letters, not by its box.
    const f32 cCapHeight = 0.52f;
    const f32 cCapLift = 0.05f;
    const f32 cCapTop = 0.19f;
    const f32 cBaseline = 0.71f;
    const f32 cGlyphDrop = 0.04f;
    // The keyboard's icons are drawn centred in their cells: one centred 0.07 of its size below the line's middle sits
    // as the game's own icons do in its text, between the capitals' middle and the small letters' (measured at 1080p).
    const f32 cKeyGlyphDrop = 0.07f;

    // The game's window with a ring (SysInfoWindowMini's): its corner texture is 24 across, so a window drawn at
    // scale k has corners of radius 24k.
    const f32 cCorner = 24.0f;

    // The game's button (BackButton): its face and ends are 64 high; the face and rim group is drawn 1.1 times larger,
    // the rim a further 1.1 times taller, and each end (32 wide) 1.1 times larger again.
    const f32 cButtonGroup = 1.1f;
    const f32 cButtonRimHeight = 64.0f * 1.1f * cButtonGroup;
    const f32 cButtonEnd = 32.0f * 1.1f;

    // The layouts the pieces are cloned from: the information window's text (the message font), the system window
    // with a ring, the game's button.
    struct Kits {
        NebulaPaneKit* mText;    // InformationWindow
        NebulaPaneKit* mWindow;  // SysInfoWindowMini
        NebulaPaneKit* mButton;  // BackButton
    };

    // A button in the game's own art (BackButton's shadow, rim, white-to-cyan face and shine), its middle stretched
    // between the two rounded ends to fit its label.
    enum { LayerShadow, LayerRim, LayerFace, LayerShine, LayerNum };
    struct Chip {
        Pane* mRoot;    // its centre, in board units
        Pane* mArt;     // the art in its authored units
        Pane* mLayers[LayerNum];
        Pane* mMiddle[LayerNum];
        Pane* mLeft[LayerNum];
        Pane* mRight[LayerNum];
        Pane* mArrow;      // Back's
        TextBox* mGlyph;   // Back's B
        TextBox* mText;
        f32 mX;
        f32 mWidth;
        f32 mHeight;
        f32 mFocusLevel;
    };

    Color8 color(u32 rgba);
    void setTextColor(TextBox* pText, u32 top, u32 bottom);
    NebulaColor16 tevColor(u32 rgba, bool alpha);
    // A window's fill and ring: its content and frame mix colour 0 (the ring) to colour 1 (the fill) by their texture.
    void paint(Window* pWindow, u32 fill, u32 ring);
    // A rounded box `w` x `h` centred on (x, y) with corners of `radius`.
    void shape(Pane* pPane, f32 x, f32 y, f32 w, f32 h, f32 radius);
    void setText(TextBox* pText, const char16_t* pString);
    void setTextSize(TextBox* pText, f32 size);
    // `anchor`: the pane's base point and the text's place in it, 0-8 row by row from the top left (4 the centre).
    void setAnchor(TextBox* pText, s32 anchor);
    void setTextStyle(TextBox* pText, f32 size, u32 top, u32 bottom, s32 anchor);
    // The text's drawn size when it may run `limit` wide (the writer breaks lines there); the box takes that size.
    void measure(TextBox* pText, f32 limit, f32* pWidth, f32* pHeight);
    // A box centred on `y` whose capitals look centred there.
    f32 textCentre(f32 y, f32 size);
    // A glyph box centred on `y` whose picture looks centred there.
    f32 glyphCentre(f32 y, f32 size);
    // The lines a measured box holds at `size`.
    s32 countLines(f32 height, f32 size);
    // A unit of text: one character, or a whole tag (its parameters may be zero).
    s32 unitLength(const char16_t* p);
    // `pSource` set broken into lines at its spaces so that none runs wider than `limit`.
    void wrapWords(TextBox* pText, const char16_t* pSource, f32 limit, f32* pWidth, f32* pHeight);
    // A box's font size for picture-font glyphs `size` high.
    Ui::Size glyphFontSize(const TextBox* pText, f32 size);
    // The game's picture font: a glyph sized like text, centred on its place.
    void setGlyph(TextBox* pText, s32 code, f32 size);
    // Where the ink of `pText`'s text (its font, at its size) is: its middle from the middle of the text's width (x) and
    // from the middle of a capital A's ink, where textCentre puts a capital's middle (y, up), and its size, in the box's
    // units. Read from the font's own glyph pictures; false when one cannot be read.
    bool inkOffset(const TextBox* pText, f32* pX, f32* pY, f32* pWidth = nullptr, f32* pHeight = nullptr);
    // `code`'s ink box as the font's shader draws it, in its cell's pixels, right and bottom its far edges (false when
    // there is none to read). A distance sheet is read at its own size, its edge where the field crosses one half.
    bool glyphInkBox(const Ui::Font* pFont, u16 code, f32* pLeft, f32* pTop, f32* pRight, f32* pBottom);
    // `pString`'s letters side by side as the font's writer sets them (`space` between them, in the font's pixels),
    // their ink boxes joined: in the font's pixels from the first cell's top left, and the cursor after the last
    // letter. False without ink, or (`strict`) when a letter's ink cannot be read.
    bool stringInkBox(const Ui::Font* pFont, const char16_t* pString, f32 space, bool strict, f32* pLeft, f32* pTop,
                      f32* pRight, f32* pBottom, f32* pEnd);
    f32 clamp(f32 v, f32 lo, f32 hi);
    f32 easeOut(f32 t);
    u8 toAlpha(f32 a);
    char16_t* append(char16_t* pDst, char16_t* pEnd, const char16_t* pSrc);
    // NebulaText's text for `pId` whole, in room that grows to hold it; its length.
    s32 getText(MR::Text16& rText, const char* pId);
    s32 getText(MR::GrowBuffer< char16_t >& rText, const char* pId);

    Window* makeWindow(const Kits& rKits, Pane* pParent, const char* pName);
    TextBox* makeText(const Kits& rKits, Pane* pParent, const char* pName, u32 length);
    Pane* makeGroup(Pane* pParent, const char* pName);
    Picture* makePicture(Pane* pParent, const char* pName, Ui::TexMap* pTexture);
    // A button in the game's art with a label of `textSize` in `textColor`; `arrow`: Back's arrow on its face.
    bool makeChip(const Kits& rKits, Pane* pParent, Chip* pChip, const char* pName, bool arrow, f32 textSize, u32 textColor);
    // The button's art for `width` x `height` (its rim's outer edge), centred on the chip's place.
    void shapeChip(Chip& rChip, f32 width, f32 height);
    // A point in screen units in the button's face layer's units.
    f32 toFaceUnits(const Chip& rChip, f32 x);
    // Back's arrow (BackButton's 48 across), as drawn at the button's height.
    f32 backArrowSize(f32 height);
    void setInfluencedAlphaRecursive(Pane* pPane, s32 depth);
    // Nebula's screens with small text ask the host for the 2D layer at the window's resolution while they show (the
    // HUD otherwise follows the internal scale, and small letters lose strokes): one bit each.
    enum { SharpBoard = 1, SharpSettings = 2, SharpPrepare = 4 };
    void setSharp(u32 screen, bool shown);
    bool isSharpScreenShown();
    // The screen in screen units (LayoutAspect's canvas, its width made square) and the root's horizontal scale.
    void readScreen(f32* pScaleX, f32* pWidth, f32* pHeight);
    // `pPane` drawn cut to the box `left`..`right`, `top`..`bottom` (in its parent's units),
    // so that a list scrolled past its window never covers what is around it. After its layout's own draw.
    void drawClipped(Pane* pPane, const Ui::DrawInfo& rDrawInfo, f32 left, f32 right, f32 top, f32 bottom);
};  // namespace NebulaUi
