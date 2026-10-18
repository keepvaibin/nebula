#ifndef GXFRAMEBUFFER_H
#define GXFRAMEBUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/gx.h"

void GXSetDispCopySrc(u16, u16, u16, u16);
void GXSetTexCopySrc(u16, u16, u16, u16);
void GXSetDispCopyDst(u16, u16);
void GXSetTexCopyDst(u16, u16, GXTexFmt, GXBool);
void GXSetDispCopyFrame2Field(GXCopyMode);
void GXSetCopyClamp(GXFBClamp);
u16 GXGetNumXfbLines(u16, f32);
f32 GXGetYScaleFactor(u16, u16);
u32 GXSetDispCopyYScale(f32);
void GXSetCopyClear(GXColor, u32);
void GXSetCopyFilter(GXBool, const u8[12][2], GXBool, const u8[7]);
void GXSetDispCopyGamma(GXGamma);
void GXCopyDisp(void*, GXBool);
void GXCopyTex(void*, GXBool);
void GXClearBoundingBox(void);

extern GXRenderModeObj GXNtsc480IntDf;
extern GXRenderModeObj GXNtsc480Int;
extern GXRenderModeObj GXMpal480IntDf;
extern GXRenderModeObj GXPal528IntDf;
extern GXRenderModeObj GXEurgb60Hz480IntDf;

#ifdef __cplusplus
}
#endif

#endif // GXFRAMEBUFFER_H
