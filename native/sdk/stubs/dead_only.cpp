// Functions that kept code references but the RMGE01 DOL does not contain (its linker dropped the callers): the
// ROM font (the IPL's font, which nothing reaches), the narrow-character text writer, two Mii database helpers and
// GXGetCullMode. Each stops the game with its name (nebula_sdk_missing) if reached.
#include <nw4r/ut/RomFont.h>
#include <nw4r/ut/TextWriterBase.h>
#include <revolution/gx.h>

#include <RFLi_DataUtility.h>

extern "C" __attribute__((noreturn)) void nebula_sdk_missing(const char* name);

namespace nw4r {
    namespace ut {
        int RomFont::GetWidth() const { nebula_sdk_missing("nw4r::ut::RomFont::GetWidth"); }
        int RomFont::GetHeight() const { nebula_sdk_missing("nw4r::ut::RomFont::GetHeight"); }
        int RomFont::GetAscent() const { nebula_sdk_missing("nw4r::ut::RomFont::GetAscent"); }
        int RomFont::GetDescent() const { nebula_sdk_missing("nw4r::ut::RomFont::GetDescent"); }
        int RomFont::GetBaselinePos() const { nebula_sdk_missing("nw4r::ut::RomFont::GetBaselinePos"); }
        int RomFont::GetCellHeight() const { nebula_sdk_missing("nw4r::ut::RomFont::GetCellHeight"); }
        int RomFont::GetCellWidth() const { nebula_sdk_missing("nw4r::ut::RomFont::GetCellWidth"); }
        int RomFont::GetMaxCharWidth() const { nebula_sdk_missing("nw4r::ut::RomFont::GetMaxCharWidth"); }
        Font::Type RomFont::GetType() const { nebula_sdk_missing("nw4r::ut::RomFont::GetType"); }
        GXTexFmt RomFont::GetTextureFormat() const { nebula_sdk_missing("nw4r::ut::RomFont::GetTextureFormat"); }
        int RomFont::GetLineFeed() const { nebula_sdk_missing("nw4r::ut::RomFont::GetLineFeed"); }
        const CharWidths RomFont::GetDefaultCharWidths() const { nebula_sdk_missing("nw4r::ut::RomFont::GetDefaultCharWidths"); }
        void RomFont::SetDefaultCharWidths(const CharWidths&) { nebula_sdk_missing("nw4r::ut::RomFont::SetDefaultCharWidths"); }
        bool RomFont::SetAlternateChar(CharCode) { nebula_sdk_missing("nw4r::ut::RomFont::SetAlternateChar"); }
        void RomFont::SetLineFeed(int) { nebula_sdk_missing("nw4r::ut::RomFont::SetLineFeed"); }
        int RomFont::GetCharWidth(CharCode) const { nebula_sdk_missing("nw4r::ut::RomFont::GetCharWidth"); }
        const CharWidths RomFont::GetCharWidths(CharCode) const { nebula_sdk_missing("nw4r::ut::RomFont::GetCharWidths"); }
        void RomFont::GetGlyph(Glyph*, CharCode) const { nebula_sdk_missing("nw4r::ut::RomFont::GetGlyph"); }
        bool RomFont::HasGlyph(CharCode) const { nebula_sdk_missing("nw4r::ut::RomFont::HasGlyph"); }
        FontEncoding RomFont::GetEncoding() const { nebula_sdk_missing("nw4r::ut::RomFont::GetEncoding"); }

        template <>
        f32 TextWriterBase< char >::PrintImpl(const char*, int) {
            nebula_sdk_missing("nw4r::ut::TextWriterBase<char>::PrintImpl");
        }
    }  // namespace ut
}  // namespace nw4r

extern "C" BOOL RFLiIsSameFaceCore(const RFLiCharInfo*, const RFLiCharInfo*) {
    nebula_sdk_missing("RFLiIsSameFaceCore");
}

extern "C" void GXGetCullMode(GXCullMode*) {
    nebula_sdk_missing("GXGetCullMode");
}
