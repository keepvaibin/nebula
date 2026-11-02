#pragma once

#include <stddef.h>

#include <NebulaGpu.h>
#include <nebula_math.h>

// BRFNT in the native layout the importer writes (crates/nebula-import cook/native_data.rs font_native): host-order
// numbers; each pointer holds its target's file offset until ResFont::Rebuild adds the file's address.
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
            u32 sheetSize;
            u16 sheetNum;
            u16 sheetFormat;
            u16 sheetRow;
            u16 sheetLine;
            u16 sheetWidth;
            u16 sheetHeight;
            u8* sheetImage;
        };

        struct FontWidth {
            u16 indexBegin;
            u16 indexEnd;
            FontWidth* pNext;
            CharWidths widthTable[];
        };

        struct FontCodeMap {
            u16 ccodeBegin;
            u16 ccodeEnd;
            u16 mappingMethod;
            u16 reserved;
            FontCodeMap* pNext;
            u16 mapInfo[];
        };

        struct FontInformation {
            u8 fontType;
            s8 linefeed;
            u16 alterCharIndex;
            CharWidths defaultWidth;
            u8 encoding;
            FontTextureGlyph* pGlyph;
            FontWidth* pWidth;
            FontCodeMap* pMap;
            u8 height;
            u8 width;
            u8 ascent;
            u8 padding_[1];
        };
        // The importer's layout (font_native).
        static_assert(sizeof(FontInformation) == 0x28 && offsetof(FontInformation, pGlyph) == 8);
        static_assert(sizeof(FontTextureGlyph) == 0x20 && offsetof(FontTextureGlyph, sheetImage) == 0x18);
        static_assert(offsetof(FontWidth, pNext) == 8 && offsetof(FontWidth, widthTable) == 0x10);
        static_assert(offsetof(FontCodeMap, pNext) == 8 && offsetof(FontCodeMap, mapInfo) == 0x10);
    };  // namespace ut
};  // namespace nw4r
