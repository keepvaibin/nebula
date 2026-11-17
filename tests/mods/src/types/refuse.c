/* Refusals of type imports (types Z-4, TY-7; L505 as the validator words it). Each mod is refused before it starts:
   tt-sig-field   (-DCASE=1): Vec3<f32>.x is f32; asked as s32: Name.FieldSig.
   tt-sig-global  (-DCASE=2): CubeCameraArea::sCubeCategory is s32; asked as u32: Name.GlobalSig.
   tt-unb-field   (-DCASE=3): AudAnmSoundObject.mHandles, in two bases: Name.FieldUnbound, LeftOut.Ambiguous.
   tt-unb-global  (-DCASE=4): Angle::cRadian090 has internal linkage: Name.GlobalUnbound.
   tt-unb-type    (-DCASE=5): MR::NameIndexOf<MR::NameKeyExact>::Slot has no size (the instance never makes it):
                  Name.TypeUnbound. BgmMuteSet was the case until the unit pass gave its .cpp's definition a row.
   tt-opt-field   (-DCASE=6): case 1 marked LUMA_OPTIONAL: the mod starts, its slot left as it was (-1). */
#include "../loader/lt.h"

#ifndef CASE
#error CASE
#endif

static int32_t slot = -1;
__attribute__((unused)) static void* address = (void*)1;

static const LumaImport imports[] = {
#if CASE == 1
    LUMA_FIELD(slot, "Vec3<f32>.x", "s32"),
#elif CASE == 2
    LUMA_GLOBAL(address, "CubeCameraArea::sCubeCategory", "u32"),
#elif CASE == 3
    LUMA_FIELD(slot, "AudAnmSoundObject.mHandles", 0),
#elif CASE == 4
    LUMA_GLOBAL(address, "Angle::cRadian090", 0),
#elif CASE == 5
    LUMA_SIZE(slot, "MR::NameIndexOf<MR::NameKeyExact>::Slot"),
#else
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "Vec3<f32>.x", "s32", &slot, 0, 0, 0},
#endif
};

LT_START(lt_fact("slot", slot))
LUMA_MOD(imports, start, 0);
