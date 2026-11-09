#pragma once

#include "Game/Ui/Font.hpp"
#include "Game/Ui/FontResourceFormat.hpp"

namespace Ui {
    namespace detail {
        class ResFontBase : public Font {
        public:
            // What a lookup reads: the file's own, or larger copies a font's owner hands it (NebulaIcons adds
            // sheets, glyphs and codes to the picture font).
            struct Tables {
                u8* pSheets;
                u16 sheetNum;
                FontGlyph* pGlyphs;
                u32 glyphNum;
                u16* pPages;
                u16* pPageBlocks;
                u32 pageBlockNum;
            };

            ResFontBase();

            virtual ~ResFontBase();
            virtual int GetWidth() const;
            virtual int GetHeight() const;
            virtual int GetAscent() const;
            virtual int GetDescent() const;
            virtual int GetBaselinePos() const;
            virtual int GetCellHeight() const;
            virtual int GetCellWidth() const;
            virtual int GetMaxCharWidth() const;
            virtual Type GetType() const;
            virtual u8 GetTextureFormat() const;
            virtual u8 GetFieldRange() const;
            virtual int GetLineFeed() const;
            virtual const CharWidths GetDefaultCharWidths() const;
            virtual void SetDefaultCharWidths(const CharWidths& widths);
            virtual bool SetAlternateChar(CharCode c);
            virtual void SetLineFeed(int linefeed);
            virtual int GetCharWidth(CharCode c) const;
            virtual const CharWidths GetCharWidths(CharCode c) const;
            virtual void GetGlyph(Glyph* glyph, CharCode c) const;
            virtual bool HasGlyph(CharCode c) const;
            virtual FontEncoding GetEncoding() const;

            void SetResourceBuffer(void* pBuffer, FontFile* pFile);
            void* RemoveResourceBuffer();
            // A code's glyph, cNoGlyph without one; GetGlyphIndex gives the alternate glyph instead.
            u16 FindGlyphIndex(CharCode c) const;
            u16 GetGlyphIndex(u16) const;
            const CharWidths& GetCharWidthsFromIndex(u16 index) const;
            void GetGlyphFromIndex(Glyph* pGlyph, u16 index) const;
            // The entry naming a code's glyph, to change it; nullptr when no page holds the code's high byte.
            u16* GlyphSlot(CharCode c) const;

            const FontFile* GetFile() const {
                return mFile;
            }

            const Tables& GetTables() const {
                return mTables;
            }

            void SetTables(const Tables& rTables) {
                mTables = rTables;
            }

            bool IsManaging(const void* pBuffer) const {
                return mResource == pBuffer;
            }

            void* mResource;
            FontFile* mFile;
            Tables mTables;
            // The codes text asked this font for without a glyph, a bit each (every code once in the log).
            mutable u32 mMissingLogged[0x10000 / 32];
        };
    };  // namespace detail
}  // namespace Ui
