#pragma once

namespace MR {
    // The text-layout test (tests/engine/text-layout*): every message of every language the disc has (its message files
    // and its system messages), page by page, laid out in the talk balloon's text box with nothing drawn, once from the
    // text of version 1 and once from its runs (NMSG version 2); each glyph's character, font, place, scale and colours,
    // each page's rectangle and printed width compared. One log line a file, then the whole; returns the pages that
    // differ (a file that cannot be read counts as one).
    u32 checkTextLayout();
}  // namespace MR
