#include "Game/Ui/ResFontBase.hpp"
#include <nebula_module.h>

namespace Ui {
    namespace detail {
        ResFontBase::ResFontBase() : mResource(nullptr), mFile(nullptr) {
            mTables.pSheets = nullptr;
            mTables.sheetNum = 0;
            mTables.pGlyphs = nullptr;
            mTables.glyphNum = 0;
            mTables.pPages = nullptr;
            mTables.pPageBlocks = nullptr;
            mTables.pageBlockNum = 0;
            for (u32 i = 0; i < sizeof(mMissingLogged) / sizeof(mMissingLogged[0]); i++) {
                mMissingLogged[i] = 0;
            }
        }

        ResFontBase::~ResFontBase() {
        }

        void ResFontBase::SetResourceBuffer(void* pBuffer, FontFile* pFile) {
            mResource = pBuffer;
            mFile = pFile;
            u8* const pBase = static_cast< u8* >(pBuffer);
            mTables.pSheets = pBase + pFile->sheets;
            mTables.sheetNum = pFile->sheetNum;
            mTables.pGlyphs = reinterpret_cast< FontGlyph* >(pBase + pFile->glyphs);
            mTables.glyphNum = pFile->glyphNum;
            mTables.pPages = reinterpret_cast< u16* >(pBase + pFile->pages);
            mTables.pPageBlocks = reinterpret_cast< u16* >(pBase + pFile->pageBlocks);
            mTables.pageBlockNum = 0;
            for (u32 hi = 0; hi < 256; hi++) {
                const u16 page = mTables.pPages[hi];
                if (page != cNoGlyph && page + 1u > mTables.pageBlockNum) {
                    mTables.pageBlockNum = page + 1u;
                }
            }
        }

        void* ResFontBase::RemoveResourceBuffer() {
            void* pUserData = mResource;
            mResource = nullptr;
            mFile = nullptr;
            mTables.pSheets = nullptr;
            mTables.sheetNum = 0;
            mTables.pGlyphs = nullptr;
            mTables.glyphNum = 0;
            mTables.pPages = nullptr;
            mTables.pPageBlocks = nullptr;
            mTables.pageBlockNum = 0;
            return pUserData;
        }

        int ResFontBase::GetWidth() const {
            return mFile->width;
        }

        int ResFontBase::GetHeight() const {
            return mFile->height;
        }

        int ResFontBase::GetAscent() const {
            return mFile->ascent;
        }

        int ResFontBase::GetDescent() const {
            return mFile->height - mFile->ascent;
        }

        int ResFontBase::GetBaselinePos() const {
            return mFile->baselinePos;
        }

        int ResFontBase::GetCellHeight() const {
            return mFile->cellHeight;
        }

        int ResFontBase::GetCellWidth() const {
            return mFile->cellWidth;
        }

        int ResFontBase::GetMaxCharWidth() const {
            return mFile->maxCharWidth;
        }

        Font::Type ResFontBase::GetType() const {
            return TYPE_RESOURCE;
        }

        u8 ResFontBase::GetTextureFormat() const {
            return static_cast< u8 >(mFile->sheetFormat);
        }

        u8 ResFontBase::GetFieldRange() const {
            return mFile->fieldRange;
        }

        int ResFontBase::GetLineFeed() const {
            return mFile->linefeed;
        }

        const CharWidths ResFontBase::GetDefaultCharWidths() const {
            return mFile->defaultWidth;
        }

        void ResFontBase::SetDefaultCharWidths(const CharWidths& widths) {
            mFile->defaultWidth = widths;
        }

        bool ResFontBase::SetAlternateChar(CharCode c) {
            const u16 index = FindGlyphIndex(c);
            if (index != cNoGlyph) {
                mFile->alterGlyph = index;
                return true;
            }
            return false;
        }

        void ResFontBase::SetLineFeed(int linefeed) {
            mFile->linefeed = static_cast< s8 >(linefeed);
        }

        int ResFontBase::GetCharWidth(CharCode c) const {
            return GetCharWidths(c).charWidth;
        }

        const CharWidths ResFontBase::GetCharWidths(CharCode c) const {
            return GetCharWidthsFromIndex(GetGlyphIndex(c));
        }

        void ResFontBase::GetGlyph(Glyph* pGlyph, u16 ch) const {
            GetGlyphFromIndex(pGlyph, GetGlyphIndex(ch));
        }

        bool ResFontBase::HasGlyph(CharCode c) const {
            return FindGlyphIndex(c) != cNoGlyph;
        }

        FontEncoding ResFontBase::GetEncoding() const {
            return static_cast< FontEncoding >(mFile->encoding);
        }

        u16 ResFontBase::GetGlyphIndex(u16 ch) const {
            const u16 index = FindGlyphIndex(ch);
            if (index != cNoGlyph) {
                return index;
            }
            // A no-break space (French "50 %") draws as the font's space; line breaking still keeps it.
            if (ch == 0x00A0 || ch == 0x202F) {
                const u16 space = FindGlyphIndex(0x20);
                if (space != cNoGlyph) {
                    return space;
                }
            }
            // Text that draws a code the font has no glyph for shows the alternate: said once a code, with the
            // font's cell (messagefont26 26x32, menufont64 59x63, cinemafont26 27x33): a tour in each language proves
            // its text drawable (tools/nebula_text.py --fonts checks the table against the game's fonts).
            u32& bits = mMissingLogged[ch >> 5];
            const u32 bit = 1u << (ch & 31u);
            if (ch >= 0x20 && (bits & bit) == 0) {
                bits |= bit;
                nebula_log("[font] no glyph U+%04X in the %ux%u font\n", static_cast< unsigned >(ch),
                             static_cast< unsigned >(mFile->cellWidth), static_cast< unsigned >(mFile->cellHeight));
            }
            return mFile->alterGlyph;
        }

        u16* ResFontBase::GlyphSlot(CharCode c) const {
            const u16 page = mTables.pPages[c >> 8];
            return page != cNoGlyph ? &mTables.pPageBlocks[page * 256u + (c & 0xFFu)] : nullptr;
        }

        u16 ResFontBase::FindGlyphIndex(u16 ch) const {
            const u16* pSlot = GlyphSlot(ch);
            return pSlot != nullptr ? *pSlot : cNoGlyph;
        }

        const CharWidths& ResFontBase::GetCharWidthsFromIndex(u16 index) const {
            return index < mTables.glyphNum ? mTables.pGlyphs[index].widths : mFile->defaultWidth;
        }

        void ResFontBase::GetGlyphFromIndex(Glyph* pGlyph, u16 index) const {
            // A glyph past the table (an alternate the font lacks) draws the first.
            const FontGlyph& rGlyph = mTables.pGlyphs[index < mTables.glyphNum ? index : 0];
            pGlyph->pTexture = mTables.pSheets + rGlyph.sheet * mFile->sheetSize;
            pGlyph->widths = rGlyph.widths;
            pGlyph->height = mFile->cellHeight;
            pGlyph->texFormat = static_cast< u8 >(mFile->sheetFormat);
            pGlyph->texWidth = mFile->sheetWidth;
            pGlyph->texHeight = mFile->sheetHeight;
            pGlyph->cellX = rGlyph.cellX;
            pGlyph->cellY = rGlyph.cellY;
            pGlyph->scale = 1.0f;
            pGlyph->lift = 0.0f;
            pGlyph->fieldRange = mFile->fieldRange;
        }
    };  // namespace detail
}  // namespace Ui
