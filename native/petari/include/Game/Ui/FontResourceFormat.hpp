#pragma once

#include <stddef.h>

#include <NebulaGpu.h>
#include <nebula_math.h>

// A font as the importer writes it (crates/nebula-import cook/native_data.rs font_native, "NFNT"): host-order
// numbers, every place an offset from the file's start. A code finds its glyph in two reads (its high byte's page, then
// that page block's entry for its low byte); a glyph names its sheet, its cell and its widths.
namespace Ui {
    struct CharWidths {
        s8 left;
        u8 glyphWidth;
        s8 charWidth;
    };

    struct FontGlyph {
        u16 sheet;
        u16 cellX;  // the cell's first pixel in the sheet, source pixels
        u16 cellY;
        CharWidths widths;
        u8 reserved;
    };

    struct FontFile {
        char signature[4];  // "NFNT"
        u32 version;        // cFontVersion
        u32 fileSize;
        u8 fontType;
        s8 linefeed;
        u16 alterGlyph;  // the glyph a code without one draws
        CharWidths defaultWidth;
        u8 encoding;
        u8 height;
        u8 width;
        u8 ascent;
        // The sheets' distance range in their texels (nebula-import cook::distance: intensity sheets are signed
        // distance fields, the edge at one half); 0 when they are pictures.
        u8 fieldRange;
        u8 cellWidth;
        u8 cellHeight;
        s8 baselinePos;
        u8 maxCharWidth;
        u16 sheetFormat;  // the source font's (SheetFormat)
        u16 sheetNum;
        u16 sheetWidth;  // source pixels (a distance sheet's record is larger; coordinates divide by these)
        u16 sheetHeight;
        u16 sheetRow;  // cells across and down a sheet
        u16 sheetLine;
        u32 sheetSize;  // bytes from one sheet's record to the next
        u32 glyphNum;
        u32 pages;       // u16[256]: a code's high byte -> its page block, cNoGlyph none
        u32 pageBlocks;  // u16[256] each: the low byte -> the glyph, cNoGlyph none
        u32 glyphs;      // FontGlyph[glyphNum]
        u32 sheets;      // the records, sheetSize apart
    };

    const u32 cFontVersion = 1;
    const u16 cNoGlyph = 0xFFFF;
    static_assert(sizeof(CharWidths) == 3 && sizeof(FontGlyph) == 10, "the importer's glyph");
    static_assert(sizeof(FontFile) == 0x40 && offsetof(FontFile, fieldRange) == 0x17 && offsetof(FontFile, sheetSize) == 0x28 &&
                      offsetof(FontFile, pages) == 0x30,
                  "the importer's header");
}  // namespace Ui
