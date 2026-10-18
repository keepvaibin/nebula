#pragma once

#include "nebula_disc.hpp"
#include <revolution.h>

// BRFNT, read in place: big-endian fields; the pointer-typed slots hold file offsets until ResFont::Rebuild relocates
// them (SelfRel natively).
namespace nw4r {
    namespace ut {
        struct CharWidths {
            s8 left;
            u8 glyphWidth;
            s8 charWidth;
        };

        struct FontTextureGlyph {
            u8 cellWidth;
            u8 cellHeight;
            s8 baselinePos;
            u8 maxCharWidth;
            NEBULA_BE(u32) sheetSize;
            NEBULA_BE(u16) sheetNum;
            NEBULA_BE(u16) sheetFormat;
            NEBULA_BE(u16) sheetRow;
            NEBULA_BE(u16) sheetLine;
            NEBULA_BE(u16) sheetWidth;
            NEBULA_BE(u16) sheetHeight;
            NEBULA_SELFREL(u8) sheetImage;
        };

        struct FontWidth {
            NEBULA_BE(u16) indexBegin;
            NEBULA_BE(u16) indexEnd;
            NEBULA_SELFREL(FontWidth) pNext;
            CharWidths widthTable[];
        };

        struct FontCodeMap {
            NEBULA_BE(u16) ccodeBegin;
            NEBULA_BE(u16) ccodeEnd;
            NEBULA_BE(u16) mappingMethod;
            NEBULA_BE(u16) reserved;
            NEBULA_SELFREL(FontCodeMap) pNext;
            NEBULA_BE(u16) mapInfo[];
        };

        struct FontInformation {
            u8 fontType;
            s8 linefeed;
            NEBULA_BE(u16) alterCharIndex;
            CharWidths defaultWidth;
            u8 encoding;
            NEBULA_SELFREL(FontTextureGlyph) pGlyph;
            NEBULA_SELFREL(FontWidth) pWidth;
            NEBULA_SELFREL(FontCodeMap) pMap;
            u8 height;
            u8 width;
            u8 ascent;
            u8 padding_[1];
        };
    };  // namespace ut
};  // namespace nw4r
