#ifndef RVL_FACE_LIBRARY_MODEL_H
#define RVL_FACE_LIBRARY_MODEL_H
#include "MiiTypes.h"
#include <NebulaGpu.h>
#include <nebula_math.h>
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct MiiMiddleDB;

typedef enum {
    MiiCoordinateType_X = 0x01000000,
    MiiCoordinateType_Y = 0x00010000,
    MiiCoordinateType_Z = 0x00000100,

    MiiCoordinateType_RevX = 0x11000000,
    MiiCoordinateType_RevY = 0x00110000,
    MiiCoordinateType_RevZ = 0x00001100,

    MiiCoordinateType_RevMask = 0x10101000
} MiiCoordinateType;

typedef struct MiiCharModel {
    // Room for Mii_CharModel, which the library keeps here (168 bytes natively: pointer members).
    union {
        u8 dummy[0xA8];
        void* align;
    };
} MiiCharModel;

typedef struct MiiDrawSetting {
    u8 lightEnable;
    u32 lightMask;
    u32 diffuse;
    u32 attn;
    NebulaColor8 ambColor;
    u8 compLoc;
} MiiDrawSetting;

// How a head draws: the position/normal matrix slot it loads its matrices into (0-9) and whether its faces cull the
// other way.
typedef struct MiiDrawCoreSetting {
    u8 matrix;
    u8 reverseCulling;
} MiiDrawCoreSetting;

u32 MiiGetModelBufferSize(MiiResolution res, u32 exprFlags);
MiiErrcode MiiInitCharModel(MiiCharModel* model, MiiDataSource src,
                            struct MiiMiddleDB* db, u16 id, void* work,
                            MiiResolution res, u32 exprFlags);
void MiiSetMtx(MiiCharModel* model, const Mtx mvMtx);
void MiiSetExpression(MiiCharModel* model, MiiExpression expr);
NebulaColor8 MiiGetFavoriteColor(MiiFavoriteColor color);
void MiiLoadDrawSetting(const MiiDrawSetting* setting);
void MiiDrawOpa(const MiiCharModel* model);
void MiiDrawXlu(const MiiCharModel* model);
;
void MiiDrawOpaCore(const MiiCharModel* model,
                    const MiiDrawCoreSetting* setting);
void MiiDrawXluCore(const MiiCharModel* model,
                    const MiiDrawCoreSetting* setting);

#ifdef __cplusplus
}
#endif
#endif
