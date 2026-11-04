#include "MiiLibInternal.h"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>

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
    J3DNativeLight light;                  // sp+168
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

    expFlag = 1 << expression;
    bufSize = MiiGetModelBufferSize(resolution, expFlag);
    modelBuf = Mii_Alloc32(bufSize);
    Mii_InitCharModel(&model, info, modelBuf, resolution, expFlag);
    MiiSetExpression(&model, expression);

    if (setting->bgType == MiiIconBG_Direct) {
        backColor = setting->bgColor;
    } else {
        backColor = MiiGetFavoriteColor(info->personal.color);
    }
    backColor.a = 0;

    J3DNativeViewGetScissor(&scissorOffsetX, &scissorOffsetY, &scissorWidth, &scissorHeight);
    J3DNativeViewSetScissor(0.0f, 0.0f, 1.0f, 1.0f);

    Mii_BeginTexture(buf, setting->width, setting->height,
                     ((u32)backColor.r << 24) | ((u32)backColor.g << 16) | ((u32)backColor.b << 8) | backColor.a);

    J3DNativeViewGetViewportv(vp);
    J3DNativeViewSetViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    aspect = (f32)setting->width / (f32)setting->height;
    if (setting->width < setting->height) {
        fovy = 2 * (RAD_TO_DEG * icon_atan2(43.2f / aspect, 500.0f));
    } else {
        fovy = 2 * (RAD_TO_DEG * icon_atan2(43.2f, 500.0f));
    }

    NebulaMtxPerspective(projMtx, fovy, aspect, 500.0f, 700.0f);
    J3DNativeViewSetPerspective(projMtx);

    cameraPos = (Vec){0.0f, 34.5f, 600.0f};
    target = (Vec){0.0f, 34.5f, 0.0f};
    cameraUp = (Vec){0.0f, 1.0f, 0.0f};

    NebulaMtxLookAt(viewMtx, &cameraPos, &cameraUp, &target);
    J3DNativeStateInitLightColor(&light, (NebulaColor8){255, 255, 255, 255});

    pos = (Vec){1600.0f, 1500.0f, 6000.0f};

    NebulaMtxMultVec(viewMtx, &pos, &pos);
    J3DNativeStateInitLightPos(&light, pos.x, pos.y, pos.z);
    J3DNativeStateSendLight(&light, 0);
    MiiSetMtx(&model, viewMtx);

    drawSetting.lightEnable = TRUE;
    drawSetting.lightMask = J3D_LIGHT0;
    drawSetting.diffuse = J3D_DF_CLAMP;
    drawSetting.attn = J3D_AF_NONE;
    drawSetting.ambColor = (NebulaColor8){160, 160, 160, 255};
    drawSetting.compLoc = 0;
    MiiLoadDrawSetting(&drawSetting);

    if (!setting->drawXluOnly) {
        J3DPassWriteColor(1);
        J3DPassWriteAlpha(1);
        J3DPassBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        MiiDrawOpa(&model);
    }

    J3DPassDepth(1, NEBULA_COMPARE_LEQUAL, 0);
    J3DPassBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    J3DPassWriteColor(1);
    J3DPassWriteAlpha(0);
    MiiDrawXlu(&model);

    J3DPassBlend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_INV_SRC_ALPHA);
    J3DPassWriteAlpha(1);
    J3DPassWriteColor(0);
    MiiDrawXlu(&model);

    J3DPassDepth(1, NEBULA_COMPARE_LEQUAL, 1);
    J3DPassWriteColor(1);
    J3DNativeTargetEnd(buf);

    Mii_Free(modelBuf);
    J3DNativeViewSetViewport(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
    J3DNativeViewSetScissor(scissorOffsetX, scissorOffsetY, scissorWidth, scissorHeight);
    Mii_SetCoordinateData(&coordData);
}

