// Functions that kept code references but the RMGE01 DOL does not contain (its linker dropped the callers): a Mii
// database helper and GXGetCullMode. Each stops the game with its name (nebula_sdk_missing) if reached.
#include <revolution/gx.h>

#include <RFLi_DataUtility.h>

extern "C" __attribute__((noreturn)) void nebula_sdk_missing(const char* name);

extern "C" BOOL RFLiIsSameFaceCore(const RFLiCharInfo*, const RFLiCharInfo*) {
    nebula_sdk_missing("RFLiIsSameFaceCore");
}

extern "C" void GXGetCullMode(GXCullMode*) {
    nebula_sdk_missing("GXGetCullMode");
}
