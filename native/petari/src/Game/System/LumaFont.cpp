#include "Game/System/LumaFont.hpp"

#include "Game/System/GameAssets.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/Ui/ResFont.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/Table.hpp"
#include <luma_modlog.h>
#include <nebula_format.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace luma::font {
    namespace {
        const u32 cMissingFontCode = 672;  // content's block (references 6)
        const u32 kMostName = 200;
        const u32 kMostKerns = 65536;      // a pair of any two codes of a full font's commonest set

        // The game fonts a mod font may fall back to (the cook's <path>.fallback names one; MessageFont26 without it).
        enum FallbackFont : u8 { kMessage, kMenu, kNumber, kPicture, kCinema };
        const char* const cFallbackNames[] = {"MessageFont26", "MenuFont64", "NumberFont", "PictureFont", "CinemaFont26"};

        Ui::Font* gameFont(u8 which) {
            switch (which) {
            case kMenu:
                return MR::getMenuFont();
            case kNumber:
                return MR::getNumberFont();
            case kPicture:
                return MR::getPictureFont();
            case kCinema:
                return MR::getCinemaFont();
            default:
                return MR::getFontOnCurrentLanguage();
            }
        }

        struct Kern {
            u32 pair;  // left << 16 | right
            s32 x;     // source pixels
        };

        // The mod's font, and for a code it has no glyph for, its fallback game font's glyph drawn at this font's height
        // with the baselines met (Glyph::scale, Glyph::lift); widths come in this font's pixels, so measuring and drawing
        // agree. The file is the store's, mapped read-only: the setters change nothing.
        class ModFont : public Ui::Font {
        public:
            ModFont() : mFallback(kMessage), mKerns(nullptr), mKernCount(0) {
            }

            virtual ~ModFont() {
            }
            virtual int GetWidth() const override {
                return mOwn.GetWidth();
            }
            virtual int GetHeight() const override {
                return mOwn.GetHeight();
            }
            virtual int GetAscent() const override {
                return mOwn.GetAscent();
            }
            virtual int GetDescent() const override {
                return mOwn.GetDescent();
            }
            virtual int GetBaselinePos() const override {
                return mOwn.GetBaselinePos();
            }
            virtual int GetCellHeight() const override {
                return mOwn.GetCellHeight();
            }
            virtual int GetCellWidth() const override {
                return mOwn.GetCellWidth();
            }
            virtual int GetMaxCharWidth() const override {
                return mOwn.GetMaxCharWidth();
            }
            virtual Type GetType() const override {
                return mOwn.GetType();
            }
            virtual u8 GetTextureFormat() const override {
                return mOwn.GetTextureFormat();
            }
            virtual u8 GetFieldRange() const override {
                return mOwn.GetFieldRange();
            }
            virtual int GetLineFeed() const override {
                return mOwn.GetLineFeed();
            }
            virtual const Ui::CharWidths GetDefaultCharWidths() const override {
                return mOwn.GetDefaultCharWidths();
            }
            virtual void SetDefaultCharWidths(const Ui::CharWidths& rWidths) override {
                (void)rWidths;
            }
            virtual bool SetAlternateChar(u16 ch) override {
                (void)ch;
                return false;
            }
            virtual void SetLineFeed(int lf) override {
                (void)lf;
            }
            virtual int GetCharWidth(u16 ch) const override {
                return GetCharWidths(ch).charWidth;
            }
            virtual const Ui::CharWidths GetCharWidths(u16 ch) const override {
                const Ui::Font* const pLender = lender(ch);
                if (pLender == nullptr) {
                    return mOwn.GetCharWidths(ch);
                }
                return scaled(pLender->GetCharWidths(ch), scaleOf(*pLender));
            }
            virtual void GetGlyph(Ui::Glyph* pGlyph, u16 ch) const override {
                const Ui::Font* const pLender = lender(ch);
                if (pLender == nullptr) {
                    mOwn.GetGlyph(pGlyph, ch);
                    return;
                }
                pLender->GetGlyph(pGlyph, ch);
                const f32 scale = scaleOf(*pLender);
                // Its picture stays its own cell (glyphWidth and height in the lender's sheet pixels): drawn `scale` times
                // as large, its baseline on this font's; left and advance in this font's pixels.
                const u8 cell = pGlyph->widths.glyphWidth;
                pGlyph->widths = scaled(pGlyph->widths, scale);
                pGlyph->widths.glyphWidth = cell;
                pGlyph->scale = scale;
                pGlyph->lift = static_cast< f32 >(mOwn.GetBaselinePos()) - static_cast< f32 >(pLender->GetBaselinePos()) * scale;
            }
            virtual bool HasGlyph(Ui::CharCode c) const override {
                return mOwn.HasGlyph(c) || lender(c) != nullptr;
            }
            virtual Ui::FontEncoding GetEncoding() const override {
                return mOwn.GetEncoding();
            }
            virtual int GetKerning(Ui::CharCode left, Ui::CharCode right) const override {
                const u32 pair = static_cast< u32 >(left) << 16 | right;
                u32 low = 0;
                u32 high = mKernCount;
                while (low < high) {
                    const u32 middle = low + (high - low) / 2u;
                    if (mKerns[middle].pair < pair) {
                        low = middle + 1u;
                    } else {
                        high = middle;
                    }
                }
                return low < mKernCount && mKerns[low].pair == pair ? mKerns[low].x : 0;
            }

            Ui::ResFont mOwn;
            u8 mFallback;
            Kern* mKerns;  // sorted by pair; ::malloc, kept with the font
            u32 mKernCount;

        private:
            // The game font that draws ch for this one: null when this one has it, or neither does (this one's
            // alternate then shows, as the cook's Note at pack said).
            const Ui::Font* lender(Ui::CharCode ch) const {
                if (mOwn.HasGlyph(ch)) {
                    return nullptr;
                }
                // A no-break space draws as this font's space (ResFontBase::GetGlyphIndex).
                if ((ch == 0x00A0 || ch == 0x202F) && mOwn.HasGlyph(0x20)) {
                    return nullptr;
                }
                const Ui::Font* const pFallback = gameFont(mFallback);
                return pFallback != nullptr && pFallback != this && pFallback->HasGlyph(ch) ? pFallback : nullptr;
            }

            f32 scaleOf(const Ui::Font& rLender) const {
                const int height = rLender.GetHeight();
                return height > 0 ? static_cast< f32 >(mOwn.GetHeight()) / static_cast< f32 >(height) : 1.0f;
            }

            static s32 round(f32 value, s32 least, s32 most) {
                const s32 at = static_cast< s32 >(value < 0.0f ? value - 0.5f : value + 0.5f);
                return at < least ? least : at > most ? most : at;
            }

            static Ui::CharWidths scaled(const Ui::CharWidths& rWidths, f32 scale) {
                Ui::CharWidths out = rWidths;
                out.left = static_cast< s8 >(round(rWidths.left * scale, -128, 127));
                out.charWidth = static_cast< s8 >(round(rWidths.charWidth * scale, -128, 127));
                out.glyphWidth = static_cast< u8 >(round(rWidths.glyphWidth * scale, 0, 255));
                return out;
            }
        };

        struct Loaded {
            Loaded* next;
            u32 hash;
            char name[kMostName];
            ModFont* font;  // null: the store lacks it or it cannot be read (said once)
        };

        Loaded* sLoaded;
        u32 sLock;
        u32 sCount;

        void lock() {
            while (__atomic_exchange_n(&sLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
            }
        }

        void unlock() {
            __atomic_store_n(&sLock, 0u, __ATOMIC_RELEASE);
        }

        // "Luma/<id>/..." -> <id>; false when the name has no id.
        bool modOf(const char* pName, char* pId, u32 size) {
            const char* const pStart = pName + 5;
            const char* const pSlash = std::strchr(pStart, '/');
            const u32 length = pSlash == nullptr ? 0u : static_cast< u32 >(pSlash - pStart);
            if (length == 0 || length >= size) {
                return false;
            }
            std::memcpy(pId, pStart, length);
            pId[length] = 0;
            return true;
        }

        void sayMissing(const char* pName) {
            char id[80];
            if (!modOf(pName, id, sizeof(id))) {
                std::strcpy(id, "?");
            }
            LumaModLogEntry entry = {};
            entry.size = sizeof(entry);
            entry.source = LUMA_MODLOG_CONTENT;
            entry.level = LUMA_MODLOG_ERROR;
            entry.code = cMissingFontCode;
            entry.mod = id;
            entry.file = "";
            entry.column = MR::getHashCode(pName);
            entry.names[0] = "found";
            entry.values[0] = pName;
            entry.count = 1;
            (void)luma::modlog::add(entry, "Content.MissingFont");
        }

        // A sidecar beside the font: its path with `pEnding` for ".brfnt"; -1 without it.
        s32 sidecar(const char* pName, const char* pEnding) {
            char path[kMostName + 16];
            const u32 length = static_cast< u32 >(std::strlen(pName));
            const u32 stem = length > 6 && MR::isEqualStringCase(pName + length - 6, ".brfnt") ? length - 6 : length;
            nebula_snprintf(path, sizeof(path), "/%.*s%s", static_cast< int >(stem), pName, pEnding);
            return GameAssets::find(path);
        }

        u8 readFallback(const char* pName) {
            const s32 entry = sidecar(pName, ".fallback");
            GameAssets::File file;
            if (entry < 0 || !GameAssets::open(entry, &file) || file.mLength == 0) {
                return kMessage;
            }
            char text[32] = {};
            const s32 size = file.mLength < sizeof(text) - 1 ? static_cast< s32 >(file.mLength) : static_cast< s32 >(sizeof(text) - 1);
            GameAssets::read(&file, text, size, 0);
            u32 end = 0;
            while (end < static_cast< u32 >(size) && text[end] > ' ' && text[end] != '.') {
                end++;
            }
            text[end] = 0;
            for (u8 i = 0; i < sizeof(cFallbackNames) / sizeof(cFallbackNames[0]); i++) {
                if (MR::isEqualStringCase(text, cFallbackNames[i])) {
                    return i;
                }
            }
            return kMessage;
        }

        void readKerns(ModFont& rFont, const char* pName) {
            const s32 entry = sidecar(pName, ".kern");
            const void* const pData = entry < 0 ? nullptr : GameAssets::map(entry);
            ::Table table;
            if (pData == nullptr || !table.attach(pData)) {
                return;
            }
            const u32 rows = static_cast< u32 >(table.getNumEntries());
            const u32 most = rows < kMostKerns ? rows : kMostKerns;
            Kern* const pKerns = most == 0 ? nullptr : static_cast< Kern* >(::malloc(most * sizeof(Kern)));
            if (pKerns == nullptr) {
                return;
            }
            u32 count = 0;
            for (u32 r = 0; r < most; r++) {
                s32 left = 0;
                s32 right = 0;
                s32 x = 0;
                if (!table.getValue(static_cast< int >(r), "left", &left) || !table.getValue(static_cast< int >(r), "right", &right) ||
                    !table.getValue(static_cast< int >(r), "x", &x) || left <= 0 || left > 0xFFFF || right <= 0 || right > 0xFFFF ||
                    x == 0) {
                    continue;
                }
                pKerns[count].pair = static_cast< u32 >(left) << 16 | static_cast< u32 >(right);
                pKerns[count].x = x;
                count++;
            }
            std::sort(pKerns, pKerns + count, [](const Kern& rA, const Kern& rB) { return rA.pair < rB.pair; });
            rFont.mKerns = pKerns;
            rFont.mKernCount = count;
        }

        ModFont* load(const char* pName) {
            char path[kMostName + 8];
            nebula_snprintf(path, sizeof(path), "/%s", pName);
            const s32 entry = GameAssets::find(path);
            const void* const pData = entry < 0 ? nullptr : GameAssets::map(entry);
            if (pData == nullptr) {
                return nullptr;
            }
            void* const pMemory = ::malloc(sizeof(ModFont));
            if (pMemory == nullptr) {
                return nullptr;
            }
            ModFont* const pFont = new (pMemory) ModFont();
            if (!pFont->mOwn.SetResource(const_cast< void* >(pData))) {
                pFont->~ModFont();
                ::free(pMemory);
                return nullptr;
            }
            pFont->InitReaderFunc(pFont->mOwn.GetEncoding());
            pFont->mFallback = readFallback(pName);
            readKerns(*pFont, pName);
            return pFont;
        }

        void loadFact(const Loaded& rLoaded) {
            char name[kMostName + 16];
            luma::modlog::jsonString(name, sizeof(name), rLoaded.name);
            char json[kMostName + 160];
            if (rLoaded.font == nullptr) {
                nebula_snprintf(json, sizeof(json), "{\"name\":%s,\"loaded\":false}", name);
            } else {
                nebula_snprintf(json, sizeof(json), "{\"name\":%s,\"loaded\":true,\"height\":%d,\"kerns\":%u,\"fallback\":\"%s\"}", name,
                                rLoaded.font->GetHeight(), rLoaded.font->mKernCount, cFallbackNames[rLoaded.font->mFallback]);
            }
            char key[32];
            nebula_snprintf(key, sizeof(key), "font.%u", sCount);
            luma::modlog::fact(key, json);
        }
    }  // namespace

    Ui::Font* find(const char* pName) {
        if (pName == nullptr || std::strncmp(pName, "Luma/", 5) != 0) {
            return nullptr;
        }
        const u32 length = static_cast< u32 >(std::strlen(pName));
        const u32 hash = MR::getHashCode(pName);
        lock();
        Loaded* pLoaded = sLoaded;
        while (pLoaded != nullptr && !(pLoaded->hash == hash && std::strcmp(pLoaded->name, pName) == 0)) {
            pLoaded = pLoaded->next;
        }
        if (pLoaded == nullptr) {
            pLoaded = static_cast< Loaded* >(::malloc(sizeof(Loaded)));
            if (pLoaded == nullptr) {
                unlock();
                return MR::getFontOnCurrentLanguage();
            }
            pLoaded->hash = hash;
            nebula_snprintf(pLoaded->name, sizeof(pLoaded->name), "%s", pName);
            pLoaded->font = length < kMostName ? load(pName) : nullptr;
            pLoaded->next = sLoaded;
            sLoaded = pLoaded;
            sCount++;
            if (pLoaded->font == nullptr) {
                sayMissing(pName);
            }
            if (luma::modlog::facts()) {
                loadFact(*pLoaded);
            }
        }
        Ui::Font* const pFont = pLoaded->font;
        unlock();
        return pFont != nullptr ? pFont : MR::getFontOnCurrentLanguage();
    }
}  // namespace luma::font
