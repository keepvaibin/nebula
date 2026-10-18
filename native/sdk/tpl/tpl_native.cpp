// TPL texture palettes natively (in place of the SDK's TPL.c), on the file-layout structs of revolution/tpl.h: big-endian
// fields and 32-bit slots relocated in place to self-relative pointers. A bound palette's version field carries a
// marker, so binding twice is harmless; the console told bound from unbound by a MEM1 address test that host pointers
// do not pass.
#include <revolution/os.h>
#include <revolution/tpl.h>

static_assert(sizeof(TPLPalette) == 12 && sizeof(TPLDescriptor) == 8, "TPL file layout");
static_assert(sizeof(TPLHeader) == 36 && sizeof(TPLClutHeader) == 12, "TPL file layout");

namespace {
    const u32 kVersion = 2142000;
    const u32 kBound = 0x4E425450; // 'NBTP'
}  // namespace

extern "C" void TPLBind(TPLPalettePtr palette) {
    const u32 version = palette->versionNumber;
    if (version == kBound) {
        return;
    }
    if (version != kVersion) {
        OSPanic(__FILE__, 0x19, "invalid version number for texture palette");
    }
    palette->descriptorArray.relocateFrom(palette);
    TPLDescriptor* descriptors = palette->descriptorArray;
    const u32 count = palette->numDescriptors;
    for (u32 i = 0; i < count; i++) {
        TPLDescriptor& d = descriptors[i];
        if (d.textureHeader.fileOffset() != 0) {
            d.textureHeader.relocateFrom(palette);
            TPLHeader* header = d.textureHeader;
            if (!header->unpacked) {
                header->data.relocateFrom(palette);
                header->unpacked = 1;
            }
        }
        if (d.CLUTHeader.fileOffset() != 0) {
            d.CLUTHeader.relocateFrom(palette);
            TPLClutHeader* clut = d.CLUTHeader;
            if (!clut->unpacked) {
                clut->data.relocateFrom(palette);
                clut->unpacked = 1;
            }
        }
    }
    palette->versionNumber = kBound;
}

extern "C" TPLDescriptorPtr TPLGet(TPLPalettePtr palette, u32 id) {
    id %= (u32)palette->numDescriptors;
    return &((TPLDescriptor*)palette->descriptorArray)[id];
}
