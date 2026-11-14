#pragma once

namespace Ui {
    class Font;
}

// A mod's own fonts (docs/modding/content/screens.md section 2a): the cook writes Luma/<id>/<path>.brfnt in the game's
// font form, <path>.kern beside it (NTBL left, right, x) and <path>.fallback (the game font that draws what the mod's
// lacks); a layout's text pane names the .brfnt.
namespace luma::font {
    // The mod font pName names ("Luma/<id>/<path>.brfnt"), loaded once and kept for the process; nullptr for any other
    // name. A mod font the store lacks or cannot read is said once (Content.MissingFont) and the message font of the
    // current language stands in. Any thread.
    Ui::Font* find(const char* pName);
}  // namespace luma::font
