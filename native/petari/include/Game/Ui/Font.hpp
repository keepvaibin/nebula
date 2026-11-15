#pragma once

#include "Game/Ui/CharStrmReader.hpp"
#include "Game/Ui/FontResourceFormat.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace Ui {
    typedef u16 CharCode;

    enum FontEncoding {
        FONT_ENCODING_UTF8,
        FONT_ENCODING_UTF16,
        FONT_ENCODING_SJIS,
        FONT_ENCODING_CP1252,

    };

    // A sheet's format as a font file names it.
    enum SheetFormat { SHEET_I4 = 0, SHEET_I8, SHEET_IA4, SHEET_IA8, SHEET_RGB565, SHEET_RGB5A3, SHEET_RGBA8 };

    struct Glyph {
        void* pTexture;
        CharWidths widths;
        u8 height;
        u8 texFormat;
        u16 texWidth;
        u16 texHeight;
        u16 cellX;
        u16 cellY;
        // A glyph another font lends (a mod font's fallback, LumaFont): its picture (glyphWidth by height in its own
        // sheet's pixels) drawn `scale` times as large and `lift` of the font's pixels lower, so its baseline meets the
        // font's; left and charWidth already in the font's pixels.
        f32 scale = 1.0f;
        f32 lift = 0.0f;
        u8 fieldRange = 0;  // its sheet's distance range (Font::GetFieldRange)
    };

    class Font {
    public:
        enum Type { TYPE_RESOURCE, };

        Font() : mReadFunc(&CharStrmReader::ReadNextCharCP1252) {
        }

        virtual ~Font() {
        }
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        virtual int GetAscent() const = 0;
        virtual int GetDescent() const = 0;
        virtual int GetBaselinePos() const = 0;
        virtual int GetCellHeight() const = 0;
        virtual int GetCellWidth() const = 0;
        virtual int GetMaxCharWidth() const = 0;
        virtual Type GetType() const = 0;
        virtual u8 GetTextureFormat() const = 0;
        // The sheets' distance range in texels, 0 when they are images (FontTextureGlyph::fieldRange).
        virtual u8 GetFieldRange() const = 0;
        virtual int GetLineFeed() const = 0;
        virtual const CharWidths GetDefaultCharWidths() const = 0;
        virtual void SetDefaultCharWidths(const CharWidths& rWidths) = 0;
        virtual bool SetAlternateChar(u16 ch) = 0;
        virtual void SetLineFeed(int lf) = 0;
        virtual int GetCharWidth(u16 ch) const = 0;
        virtual const CharWidths GetCharWidths(u16 ch) const = 0;
        virtual void GetGlyph(Glyph* pGlyph, u16 ch) const = 0;
        virtual bool HasGlyph(CharCode c) const = 0;
        virtual FontEncoding GetEncoding() const = 0;
        // What is added between `left` and `right` written in a row, in the font's pixels (a mod font's .kern; the
        // game's fonts have none).
        virtual int GetKerning(CharCode left, CharCode right) const {
            (void)left;
            (void)right;
            return 0;
        }

        void InitReaderFunc(FontEncoding);

        const CharStrmReader GetCharStrmReader() const {
            CharStrmReader reader(mReadFunc);
            return reader;
        }

        CharStrmReader::ReadNextCharFunc mReadFunc;
    };
}  // namespace Ui
