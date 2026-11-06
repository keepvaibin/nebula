#include "MiiLibInternal.h"
#include "MiiNative.h"

static const f32 RAD_TO_DEG = 57.29578f;

double atan2(double y, double x);

static inline float icon_atan2(float x, float y) {
    return atan2(x, y);
}

MiiErrcode MiiMakeIcon(void* buf, MiiDataSource source, MiiMiddleDB* middleDB, u16 index, MiiExpression expression, const MiiIconSetting* setting) {
    Mii_CharInfo info;
    MiiErrcode err = Mii_PickupCharInfo(&info, source, middleDB, index);

    if (err == MiiErrcode_Success) {
        Mii_MakeIcon(buf, &info, expression, setting);
    }

    return err;
}

void Mii_MakeIcon(void* buf, Mii_CharInfo* info, MiiExpression expression, const MiiIconSetting* setting) {
    MiiCharModel model;                // sp+218
    void* modelBuf;                    // sp+84
    f32 vp[6];                         // sp+150
    Mtx viewMtx;                       // sp+1e8
    f32 scissorOffsetX;                // sp+7C
    f32 scissorOffsetY;                // sp+78
    f32 scissorWidth;                  // sp+74
    f32 scissorHeight;                 // sp+70
    Mii_CoordinateData iconCoordData;  // sp+118
    Mii_CoordinateData coordData;      // sp+128
    u32 bufSize;                       // sp+6C
    MiiResolution resolution;          // sp+68
    MiiExpFlag expFlag;                // sp+64
    NebulaColor8 backColor;                 // sp+60
    Mtx44 projMtx;                     // sp+1A8
    f32 fovy;                          // f31
    f32 aspect;                        // f30
    Vec cameraPos;                     // sp+10C
    Vec target;                        // sp+100
    Vec cameraUp;                      // sp+F4
    NebulaLight light = {};                  // sp+168
    Vec pos;                           // sp+E8
    MiiDrawSetting drawSetting;        // sp+138

    // buf -> r28
    // info -> r29
    // expression -> r27
    // setting -> r30

    iconCoordData = (Mii_CoordinateData){1, 2, 0, FALSE, FALSE, FALSE};
    coordData = *Mii_GetCoordinateData();
    Mii_SetCoordinateData(&iconCoordData);

    if ((setting->width > 128 || setting->height > 128) && Mii_GetUseDeluxTex()) {
        resolution = MiiResolution_256;
    } else if (setting->width > 64 || setting->height > 64) {
        resolution = MiiResolution_128;
    } else {
        resolution = MiiResolution_64;
    }

    expFlag = static_cast< MiiExpFlag >(1 << expression);
    bufSize = MiiGetModelBufferSize(resolution, expFlag);
    modelBuf = Mii_Alloc32(bufSize);
    Mii_InitCharModel(&model, info, modelBuf, resolution, expFlag);
    MiiSetExpression(&model, expression);

    if (setting->bgType == MiiIconBG_Direct) {
        backColor = setting->bgColor;
    } else {
        backColor = MiiGetFavoriteColor(static_cast< MiiFavoriteColor >(info->personal.color));
    }
    backColor.a = 0;

    Frame::getScissor(&scissorOffsetX, &scissorOffsetY, &scissorWidth, &scissorHeight);
    Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);

    Mii_BeginTexture(buf, setting->width, setting->height,
                     ((u32)backColor.r << 24) | ((u32)backColor.g << 16) | ((u32)backColor.b << 8) | backColor.a);

    Frame::getViewportv(vp);
    Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    aspect = (f32)setting->width / (f32)setting->height;
    if (setting->width < setting->height) {
        fovy = 2 * (RAD_TO_DEG * icon_atan2(43.2f / aspect, 500.0f));
    } else {
        fovy = 2 * (RAD_TO_DEG * icon_atan2(43.2f, 500.0f));
    }

    NebulaMtxPerspective(projMtx, fovy, aspect, 500.0f, 700.0f);
    Frame::setPerspective(projMtx);

    cameraPos = (Vec){0.0f, 34.5f, 600.0f};
    target = (Vec){0.0f, 34.5f, 0.0f};
    cameraUp = (Vec){0.0f, 1.0f, 0.0f};

    NebulaMtxLookAt(viewMtx, &cameraPos, &cameraUp, &target);
    LightValues::color(&light, (NebulaColor8){255, 255, 255, 255});

    pos = (Vec){1600.0f, 1500.0f, 6000.0f};

    NebulaMtxMultVec(viewMtx, &pos, &pos);
    LightValues::pos(&light, pos.x, pos.y, pos.z);
    Frame::setLight(0, light);
    MiiSetMtx(&model, viewMtx);

    drawSetting.lightEnable = TRUE;
    drawSetting.lightMask = MODEL_FILE_LIGHT0;
    drawSetting.diffuse = MODEL_FILE_DF_CLAMP;
    drawSetting.attn = MODEL_FILE_AF_NONE;
    drawSetting.ambColor = (NebulaColor8){160, 160, 160, 255};
    drawSetting.compLoc = 0;
    MiiLoadDrawSetting(&drawSetting);

    if (!setting->drawXluOnly) {
        Mii_NativeState(PassStates::use(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 1, NEBULA_CULL_NONE, 1, 1));
        MiiDrawOpa(&model);
    }

    Mii_NativeState(PassStates::use(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_NONE, 1, 0));
    MiiDrawXlu(&model);

    Mii_NativeState(PassStates::use(NEBULA_FACTOR_ONE, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_NONE, 0, 1));
    MiiDrawXlu(&model);

    Frame::targetEnd(buf);

    Mii_Free(modelBuf);
    Frame::setViewport(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
    Frame::setScissor(scissorOffsetX, scissorOffsetY, scissorWidth, scissorHeight);
    Mii_SetCoordinateData(&coordData);
}

