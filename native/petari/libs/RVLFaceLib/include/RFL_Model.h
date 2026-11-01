#ifndef RVL_FACE_LIBRARY_MODEL_H
#define RVL_FACE_LIBRARY_MODEL_H
#include "RFL_Types.h"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct RFLMiddleDB;

typedef enum {
    RFLCoordinateType_X = 0x01000000,
    RFLCoordinateType_Y = 0x00010000,
    RFLCoordinateType_Z = 0x00000100,

    RFLCoordinateType_RevX = 0x11000000,
    RFLCoordinateType_RevY = 0x00110000,
    RFLCoordinateType_RevZ = 0x00001100,

    RFLCoordinateType_RevMask = 0x10101000
} RFLCoordinateType;

typedef struct RFLCharModel {
    // Room for RFLiCharModel, which the library keeps here (168 bytes natively: pointer members).
    union {
        u8 dummy[0xA8];
        void* align;
    };
} RFLCharModel;

typedef struct RFLDrawSetting {
    u8 lightEnable;      // at 0x0
    GXLightID lightMask; // at 0x4
    GXDiffuseFn diffuse; // at 0x8
    GXAttnFn attn;       // at 0xC
    GXColor ambColor;    // at 0x10
    u8 compLoc;          // at 0x14
} RFLDrawSetting;

// How a head draws: the position/normal matrix slot it loads its matrices into (0-9) and whether its faces cull the
// other way.
typedef struct RFLDrawCoreSetting {
    u8 matrix;
    u8 reverseCulling;
} RFLDrawCoreSetting;

u32 RFLGetModelBufferSize(RFLResolution res, u32 exprFlags);
RFLErrcode RFLInitCharModel(RFLCharModel* model, RFLDataSource src,
                            struct RFLMiddleDB* db, u16 id, void* work,
                            RFLResolution res, u32 exprFlags);
void RFLSetMtx(RFLCharModel* model, const Mtx mvMtx);
void RFLSetExpression(RFLCharModel* model, RFLExpression expr);
GXColor RFLGetFavoriteColor(RFLFavoriteColor color);
void RFLLoadDrawSetting(const RFLDrawSetting* setting);
void RFLDrawOpa(const RFLCharModel* model);
void RFLDrawXlu(const RFLCharModel* model);
    NO_INLINE;
void RFLDrawOpaCore(const RFLCharModel* model,
                    const RFLDrawCoreSetting* setting);
void RFLDrawXluCore(const RFLCharModel* model,
                    const RFLDrawCoreSetting* setting);

#ifdef __cplusplus
}
#endif
#endif
