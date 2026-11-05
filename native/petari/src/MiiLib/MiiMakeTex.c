#include "MiiLibInternal.h"
#include <stdlib.h>
#include "MiiNative.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <math.h>

// The drawing state a Mii texture is drawn with.
void Mii_SetupTextureDraw(void) {
    J3DPassWriteColor(1);
    J3DPassWriteAlpha(1);
    J3DPassDepth(1, NEBULA_COMPARE_LEQUAL, 1);
}

// A Mii texture drawn into `buffer` (E-07): a target pass `width` x `height` cleared to `clearRgba` (R << 24 | G << 16 | B << 8 | A); the texture is
// the renderer's picture of the pass (J3DNativeTargetEnd), sampled where the game names `buffer`.
void Mii_BeginTexture(void* buffer, u16 width, u16 height, u32 clearRgba) {
    Mii_SetupTextureDraw();
    // Twice the texels the parts are laid out in, whatever the frame's size.
    J3DNativeTargetBeginPixels(buffer, width * 2u, height * 2u, clearRgba);
}

void Mii_MakeTexture(const Mii_CharInfo* info, u8** buffers, MiiResolution resolution) {
    int i;
    Mii_CharInfo* const infos = malloc(sizeof(Mii_CharInfo) * MiiExp_Max);
    Mii_FaceParts* const parts = malloc(sizeof(Mii_FaceParts) * MiiExp_Max);
    u8** eyeBuf[MiiExp_Max];
    u8** mouthBuf[MiiExp_Max];
    Mii_Texture** eyeTex[MiiExp_Max];
    Mii_Texture** mouthTex[MiiExp_Max];

    u32 eyeSize;
    u32 eyebrowSize;
    u32 mouthSize;
    u32 mustacheSize;
    u32 moleSize;

    u8* eyeNormal;
    u8* eyebrowNormal;
    u8* mouthNormal;
    u8* mustacheNormal;
    u8* moleNormal;

    u8* eyeSmile;
    u8* mouthAnger;
    u8* eyeSorrow;
    u8* mouthSorrow;
    u8* eyeSurprise;
    u8* eyeBlink;
    u8* mouthOpen;

    Mii_Texture* eyeNormalTex;
    Mii_Texture* eyebrowNormalTex;
    Mii_Texture* mouthNormalTex;
    Mii_Texture* mustacheNormalTex;
    Mii_Texture* moleNormalTex;
    Mii_Texture* eyeSmileTex;
    Mii_Texture* mouthAngerTex;
    Mii_Texture* eyeSadTex;
    Mii_Texture* mouthSadTex;
    Mii_Texture* eyeSurpriseTex;
    Mii_Texture* eyeBlinkTex;
    Mii_Texture* mouthOpenTex;

    f32 sx;
    f32 sy;
    f32 sw;
    f32 sh;

    u8* ptr;

    eyeSize = Mii_GetTexSize(Mii_PartsTex_Eye, 0);
    eyebrowSize = Mii_GetTexSize(Mii_PartsTex_Eyebrow, 0);
    mouthSize = Mii_GetTexSize(Mii_PartsTex_Mouth, 0);
    mustacheSize = Mii_GetTexSize(Mii_PartsTex_Mustache, 0);
    moleSize = Mii_GetTexSize(Mii_PartsTex_Mole, 0);

    eyeNormal = NULL;
    eyebrowNormal = NULL;
    mouthNormal = NULL;
    mustacheNormal = NULL;
    moleNormal = NULL;

    eyeSmile = NULL;
    mouthAnger = NULL;
    eyeSorrow = NULL;
    mouthSorrow = NULL;
    eyeSurprise = NULL;
    eyeBlink = NULL;
    mouthOpen = NULL;

    eyeBuf[MiiExp_Normal] = &eyeNormal;
    eyeBuf[MiiExp_Smile] = &eyeSmile;
    eyeBuf[MiiExp_Anger] = &eyeNormal;
    eyeBuf[MiiExp_Sorrow] = &eyeSorrow;
    eyeBuf[MiiExp_Surprise] = &eyeSurprise;
    eyeBuf[MiiExp_Blink] = &eyeBlink;
    eyeBuf[MiiExp_OpenMouth] = &eyeNormal;

    mouthBuf[MiiExp_Normal] = &mouthNormal;
    mouthBuf[MiiExp_Smile] = &mouthNormal;
    mouthBuf[MiiExp_Anger] = &mouthAnger;
    mouthBuf[MiiExp_Sorrow] = &mouthSorrow;
    mouthBuf[MiiExp_Surprise] = &mouthNormal;
    mouthBuf[MiiExp_Blink] = &mouthNormal;
    mouthBuf[MiiExp_OpenMouth] = &mouthOpen;

    eyeNormalTex = NULL;
    eyebrowNormalTex = NULL;
    mouthNormalTex = NULL;
    mustacheNormalTex = NULL;
    moleNormalTex = NULL;
    eyeSmileTex = NULL;
    mouthAngerTex = NULL;
    eyeSadTex = NULL;
    mouthSadTex = NULL;
    eyeSurpriseTex = NULL;
    eyeBlinkTex = NULL;
    mouthOpenTex = NULL;

    eyeTex[MiiExp_Normal] = &eyeNormalTex;
    eyeTex[MiiExp_Smile] = &eyeSmileTex;
    eyeTex[MiiExp_Anger] = &eyeNormalTex;
    eyeTex[MiiExp_Sorrow] = &eyeSadTex;
    eyeTex[MiiExp_Surprise] = &eyeSurpriseTex;
    eyeTex[MiiExp_Blink] = &eyeBlinkTex;
    eyeTex[MiiExp_OpenMouth] = &eyeNormalTex;

    mouthTex[MiiExp_Normal] = &mouthNormalTex;
    mouthTex[MiiExp_Smile] = &mouthNormalTex;
    mouthTex[MiiExp_Anger] = &mouthAngerTex;
    mouthTex[MiiExp_Sorrow] = &mouthSadTex;
    mouthTex[MiiExp_Surprise] = &mouthNormalTex;
    mouthTex[MiiExp_Blink] = &mouthNormalTex;
    mouthTex[MiiExp_OpenMouth] = &mouthOpenTex;

    if (buffers[MiiExp_Normal] != NULL) {
        infos[MiiExp_Normal] = *info;
    }

    if (buffers[MiiExp_Smile] != NULL) {
        int changeEyeRot = 0;
        MiiExpression expr = MiiExp_Smile;

        infos[expr] = *info;
        infos[expr].eye.type = 48;

        changeEyeRot = MII_I_EYE_ROT_OFFSET[info->eye.type] - MII_I_EYE_ROT_OFFSET[infos[expr].eye.type];
        if (changeEyeRot + (int)infos[expr].eye.rotate < 0) {
            infos[expr].eye.rotate = 0;
        } else if (changeEyeRot + (int)infos[expr].eye.rotate > MII_I_MAX_EYE_ROTATE) {
            infos[expr].eye.rotate = MII_I_MAX_EYE_ROTATE;
        } else {
            infos[expr].eye.rotate += changeEyeRot;
        }
    }

    if (buffers[MiiExp_Anger] != NULL) {
        int changeEyebrowRot = 0;
        int changeEyeRot = 0;
        MiiExpression expr = MiiExp_Anger;

        infos[expr] = *info;

        changeEyebrowRot = 2;
        if (changeEyebrowRot + (int)infos[expr].eyebrow.rotate < 0) {
            infos[expr].eyebrow.rotate = 0;
        } else if (changeEyebrowRot + (int)infos[expr].eyebrow.rotate > MII_I_MAX_EYEBROW_ROTATE) {
            infos[expr].eyebrow.rotate = MII_I_MAX_EYEBROW_ROTATE;
        } else {
            infos[expr].eyebrow.rotate += changeEyebrowRot;
        }

        changeEyeRot = 2;
        if (changeEyeRot + (int)infos[expr].eye.rotate < 0) {
            infos[expr].eye.rotate = 0;
        } else if (changeEyeRot + (int)infos[expr].eye.rotate > MII_I_MAX_EYE_ROTATE) {
            infos[expr].eye.rotate = MII_I_MAX_EYE_ROTATE;
        } else {
            infos[expr].eye.rotate += changeEyeRot;
        }

        infos[expr].mouth.type = 10;
    }

    if (buffers[MiiExp_Sorrow] != NULL) {
        int changeEyebrowRot = 0;
        int changeEyeRot = 0;
        MiiExpression expr = MiiExp_Sorrow;

        infos[expr] = *info;

        changeEyebrowRot = -2;
        if (changeEyebrowRot + (int)infos[expr].eyebrow.rotate < 0) {
            infos[expr].eyebrow.rotate = 0;
        } else if (changeEyebrowRot + (int)infos[expr].eyebrow.rotate > MII_I_MAX_EYEBROW_ROTATE) {
            infos[expr].eyebrow.rotate = MII_I_MAX_EYEBROW_ROTATE;
        } else {
            infos[expr].eyebrow.rotate += changeEyebrowRot;
        }

        changeEyeRot = -2;
        if (changeEyeRot + (int)infos[expr].eye.rotate < 0) {
            infos[expr].eye.rotate = 0;
        } else if (changeEyeRot + (int)infos[expr].eye.rotate > MII_I_MAX_EYE_ROTATE) {
            infos[expr].eye.rotate = MII_I_MAX_EYE_ROTATE;
        } else {
            infos[expr].eye.rotate += changeEyeRot;
        }

        infos[expr].mouth.type = 12;
    }

    if (buffers[MiiExp_Surprise] != NULL) {
        int changeEyeRot = 0;
        MiiExpression expr = MiiExp_Surprise;

        infos[expr] = *info;
        infos[expr].eyebrow.y -= 2;
        infos[expr].eye.type = 49;

        changeEyeRot = MII_I_EYE_ROT_OFFSET[info->eye.type] - MII_I_EYE_ROT_OFFSET[infos[expr].eye.type];
        if (changeEyeRot + (int)infos[expr].eye.rotate < 0) {
            infos[expr].eye.rotate = 0;
        } else if (changeEyeRot + (int)infos[expr].eye.rotate > MII_I_MAX_EYE_ROTATE) {
            infos[expr].eye.rotate = MII_I_MAX_EYE_ROTATE;
        } else {
            infos[expr].eye.rotate += changeEyeRot;
        }
    }

    if (buffers[MiiExp_Blink] != NULL) {
        int changeEyeRot = 0;
        MiiExpression expr = MiiExp_Blink;

        infos[expr] = *info;
        infos[expr].eye.type = 26;

        changeEyeRot = MII_I_EYE_ROT_OFFSET[info->eye.type] - MII_I_EYE_ROT_OFFSET[infos[expr].eye.type];
        if (changeEyeRot + (int)infos[expr].eye.rotate < 0) {
            infos[expr].eye.rotate = 0;
        } else if (changeEyeRot + (int)infos[expr].eye.rotate > MII_I_MAX_EYE_ROTATE) {
            infos[expr].eye.rotate = MII_I_MAX_EYE_ROTATE;
        } else {
            infos[expr].eye.rotate += changeEyeRot;
        }
    }

    if (buffers[MiiExp_OpenMouth] != NULL) {
        MiiExpression expr = MiiExp_OpenMouth;

        infos[expr] = *info;
        infos[expr].mouth.type = 24;
    }

    for (i = 0; i < MiiExp_Max; i++) {
        if (buffers[i] == NULL) {
            continue;
        }

        if (*eyeBuf[i] == NULL) {
            *eyeBuf[i] = Mii_Alloc32(eyeSize);
        }

        if (*mouthBuf[i] == NULL) {
            *mouthBuf[i] = Mii_Alloc32(mouthSize);
        }

        if (eyebrowNormal == NULL) {
            eyebrowNormal = Mii_Alloc32(eyebrowSize);
        }

        if (mustacheNormal == NULL) {
            mustacheNormal = Mii_Alloc32(mustacheSize);
        }

        if (moleNormal == NULL) {
            moleNormal = Mii_Alloc32(moleSize);
        }

        if (*eyeTex[i] == NULL) {
            *eyeTex[i] = Mii_LoadTexture(Mii_PartsTex_Eye, infos[i].eye.type, *eyeBuf[i]);
        }

        if (*mouthTex[i] == NULL) {
            *mouthTex[i] = Mii_LoadTexture(Mii_PartsTex_Mouth, infos[i].mouth.type, *mouthBuf[i]);
        }

        if (eyebrowNormalTex == NULL) {
            eyebrowNormalTex = Mii_LoadTexture(Mii_PartsTex_Eyebrow, infos[i].eyebrow.type, eyebrowNormal);
        }

        if (mustacheNormalTex == NULL) {
            mustacheNormalTex = Mii_LoadTexture(Mii_PartsTex_Mustache, infos[i].beard.mustache, mustacheNormal);
        }

        if (moleNormalTex == NULL) {
            moleNormalTex = Mii_LoadTexture(Mii_PartsTex_Mole, infos[i].mole.type, moleNormal);
        }

        parts[i].eyeR.ngtobj.texture = *eyeTex[i];
        parts[i].eyeL.ngtobj.texture = *eyeTex[i];

        parts[i].eyebrowR.ngtobj.texture = eyebrowNormalTex;
        parts[i].eyebrowL.ngtobj.texture = eyebrowNormalTex;

        parts[i].mouth.ngtobj.texture = *mouthTex[i];

        parts[i].mustacheR.ngtobj.texture = mustacheNormalTex;
        parts[i].mustacheL.ngtobj.texture = mustacheNormalTex;

        parts[i].mole.ngtobj.texture = moleNormalTex;

        Mii_InitPartTexture(&parts[i].eyeR.ngtobj);
        Mii_InitPartTexture(&parts[i].eyeL.ngtobj);

        Mii_InitPartTexture(&parts[i].eyebrowR.ngtobj);
        Mii_InitPartTexture(&parts[i].eyebrowL.ngtobj);

        Mii_InitPartTexture(&parts[i].mouth.ngtobj);

        Mii_InitPartTexture(&parts[i].mustacheR.ngtobj);
        Mii_InitPartTexture(&parts[i].mustacheL.ngtobj);

        Mii_InitPartTexture(&parts[i].mole.ngtobj);
    }

    for (i = 0; i < MiiExp_Max; i++) {
        if (buffers[i] != NULL) {
            break;
        }
    }

    Mii_SetupTextureDraw();

    J3DNativeViewGetScissor(&sx, &sy, &sw, &sh);
    for (i = 0; i < MiiExp_Max; i++) {
        if (buffers[i] == NULL) {
            continue;
        }

        ptr = buffers[i];

        if (resolution & 256) {
            J3DNativeViewSetScissor(0.0f, 0.0f, 1.0f, 1.0f);
            Mii_SetFaceParts(&infos[i], &parts[i], MII_I_MASKRSL_256);
            Mii_BeginTexture(ptr, 256, 256, 0x00000000);
            Mii_Capture(ptr, &infos[i], &parts[i], MII_I_MASKRSL_256);
            J3DNativeTargetEnd(ptr);
            ptr += Mii_GetMaskSize(MII_I_MASKRSL_256);
        }

        if (resolution & 128) {
            J3DNativeViewSetScissor(0.0f, 0.0f, 1.0f, 1.0f);
            Mii_SetFaceParts(&infos[i], &parts[i], MII_I_MASKRSL_128);
            Mii_BeginTexture(ptr, 128, 128, 0x00000000);
            Mii_Capture(ptr, &infos[i], &parts[i], MII_I_MASKRSL_128);
            J3DNativeTargetEnd(ptr);
            ptr += Mii_GetMaskSize(MII_I_MASKRSL_128);
        }

        if (resolution & 64) {
            J3DNativeViewSetScissor(0.0f, 0.0f, 1.0f, 1.0f);
            Mii_SetFaceParts(&infos[i], &parts[i], MII_I_MASKRSL_64);
            Mii_BeginTexture(ptr, 64, 64, 0x00000000);
            Mii_Capture(ptr, &infos[i], &parts[i], MII_I_MASKRSL_64);
            J3DNativeTargetEnd(ptr);
            ptr += Mii_GetMaskSize(MII_I_MASKRSL_64);
        }

        if (resolution & 32) {
            J3DNativeViewSetScissor(0.0f, 0.0f, 1.0f, 1.0f);
            Mii_SetFaceParts(&infos[i], &parts[i], MII_I_MASKRSL_32);
            Mii_BeginTexture(ptr, 32, 32, 0x00000000);
            Mii_Capture(ptr, &infos[i], &parts[i], MII_I_MASKRSL_32);
            J3DNativeTargetEnd(ptr);
            ptr += Mii_GetMaskSize(MII_I_MASKRSL_32);
        }
    }

    if (eyeNormal != NULL) {
        Mii_Free(eyeNormal);
    }

    if (eyebrowNormal != NULL) {
        Mii_Free(eyebrowNormal);
    }

    if (mouthNormal != NULL) {
        Mii_Free(mouthNormal);
    }

    if (mustacheNormal != NULL) {
        Mii_Free(mustacheNormal);
    }

    if (moleNormal != NULL) {
        Mii_Free(moleNormal);
    }

    if (eyeSmile != NULL) {
        Mii_Free(eyeSmile);
    }

    if (mouthAnger != NULL) {
        Mii_Free(mouthAnger);
    }

    if (eyeSorrow != NULL) {
        Mii_Free(eyeSorrow);
    }

    if (mouthSorrow != NULL) {
        Mii_Free(mouthSorrow);
    }

    if (eyeSurprise != NULL) {
        Mii_Free(eyeSurprise);
    }

    if (eyeBlink != NULL) {
        Mii_Free(eyeBlink);
    }

    if (mouthOpen != NULL) {
        Mii_Free(mouthOpen);
    }

    J3DNativeViewSetScissor(sx, sy, sw, sh);

    free(infos);
    free(parts);
}

void Mii_InitPartTexture(Mii_TexObj* tobj) {
    Mii_Texture* tex = tobj->texture;

    J3DNativeTextureInit(&tobj->tobj, Mii_GetTexImage(tex), tex->width, tex->height, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    J3DNativeTextureSetFilter(&tobj->tobj, tex->minFilt, tex->magFilt, tex->minLOD, tex->maxLOD, tex->lodBias, tex->enableEdgeLOD, tex->enableMaxAniso);
}

void Mii_Setup2DCameraAndParam(f32 size) {
    Mtx44 proj;

    NebulaMtxOrtho(proj, 0.0f, size, 0.0f, size, 0.0f, 1.0f);
    J3DNativeViewSetOrthographic(proj);
    J3DNativeViewSetViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    J3DNativeViewSetZScaleOffset(1.0f, 0.0f);
    J3DPassCull(NEBULA_CULL_BACK);
    J3DPassDepth(0, NEBULA_COMPARE_LEQUAL, 0);
    J3DPassWriteAlpha(1);
}

// The parts' forms (runtime/src/render/materials/mii_texture.hlsl), with their colours.
void Mii_SetStage4Mole(void) {
    Mii_NativeTextureForm(0, MII_I_MOLE_COLOR0, MII_I_MOLE_COLOR0);
}

void Mii_SetStage4Mouth(u32 color) {
    Mii_NativeTextureForm(1, MII_I_MOUTH_COLOR0[color], MII_I_MOUTH_COLOR1[color]);
}

void Mii_SetStage4Eye(u32 color, u32 type) {
    NebulaColor8 color0;

    switch (type) {
    case 9:
        color0 = (NebulaColor8){255, 130, 0, 255};
        break;
    case 20:
        color0 = (NebulaColor8){0, 255, 255, 255};
        break;
    default:
        color0 = (NebulaColor8){0, 0, 0, 255};
        break;
    }

    Mii_NativeTextureForm(2, color0, MII_I_EYE_COLOR1[color]);
}

void Mii_SetStage4Eyebrow(u32 color) {
    Mii_NativeTextureForm(0, MII_I_EYEBROW_COLOR0[color], MII_I_EYEBROW_COLOR0[color]);
}

void Mii_SetStage4Mustache(u32 color) {
    Mii_NativeTextureForm(0, MII_I_BEARD_COLOR0[color], MII_I_BEARD_COLOR0[color]);
}

void Mii_SetFaceParts(const Mii_CharInfo* info, Mii_FaceParts* face, MII_I_MASKRSL resolution) {
    f32 eyeX = MII_I_TEX_EYE_BASE_X + MII_I_TEX_SCALE_X * info->eye.x;
    f32 eyeY = MII_I_TEX_EYE_BASE_Y + 1.1600001f * MII_I_TEX_SCALE_Y * info->eye.y;
    f32 eyeW = MII_I_TEX_EYE_BASE_W * MII_I_TEX_SCALE2DIM(info->eye.scale);
    f32 eyeH = MII_I_TEX_EYE_BASE_H * MII_I_TEX_SCALE2DIM(info->eye.scale);
    f32 eyeA = MII_I_TEX_ROTATE2ANG(info->eye.rotate + MII_I_EYE_ROT_OFFSET[info->eye.type]);

    f32 eyebrowX = MII_I_TEX_EYEBROW_BASE_X + MII_I_TEX_SCALE_X * info->eyebrow.x;
    f32 eyebrowY = MII_I_TEX_EYEBROW_BASE_Y + 1.1600001f * MII_I_TEX_SCALE_Y * info->eyebrow.y;
    f32 eyebrowW = MII_I_TEX_EYEBROW_BASE_W * MII_I_TEX_SCALE2DIM(info->eyebrow.scale);
    f32 eyebrowH = MII_I_TEX_EYEBROW_BASE_H * MII_I_TEX_SCALE2DIM(info->eyebrow.scale);
    f32 eyebrowA = MII_I_TEX_ROTATE2ANG(info->eyebrow.rotate + MII_I_EYEBROW_ROT_OFFSET[info->eyebrow.type]);

    f32 mouthY = MII_I_TEX_MOUTH_BASE_Y + 1.1600001f * MII_I_TEX_SCALE_Y * info->mouth.y;
    f32 mouthW = MII_I_TEX_MOUTH_BASE_W * MII_I_TEX_SCALE2DIM(info->mouth.scale);
    f32 mouthH = MII_I_TEX_MOUTH_BASE_H * MII_I_TEX_SCALE2DIM(info->mouth.scale);

    f32 mustacheY = MII_I_TEX_MUSTACHE_BASE_Y + 1.1600001f * MII_I_TEX_SCALE_Y * info->beard.y;
    f32 mustacheW = MII_I_TEX_MUSTACHE_BASE_W * MII_I_TEX_SCALE2DIM(info->beard.scale);
    f32 mustacheH = MII_I_TEX_MUSTACHE_BASE_H * MII_I_TEX_SCALE2DIM(info->beard.scale);

    f32 moleX = MII_I_TEX_MOLE_BASE_X + 2.0f * MII_I_TEX_SCALE_X * info->mole.x;
    f32 moleY = MII_I_TEX_MOLE_BASE_Y + 1.1600001f * MII_I_TEX_SCALE_Y * info->mole.y;
    f32 moleW = MII_I_TEX_MOLE_BASE_W + MII_I_TEX_SCALE2DIM(info->mole.scale);
    f32 moleH = MII_I_TEX_MOLE_BASE_H + MII_I_TEX_SCALE2DIM(info->mole.scale);

    f32 resolution_ = MII_I_TEX_UNIT(resolution);

    face->eyeR.x = resolution_ * (32.0f - eyeX);
    face->eyeR.y = eyeY * resolution_;
    face->eyeR.width = eyeW * resolution_;
    face->eyeR.height = eyeH * resolution_;
    face->eyeR.angle = eyeA;
    face->eyeR.origin = MII_ORIGIN_RIGHT;

    face->eyeL.x = resolution_ * (32.0f + eyeX);
    face->eyeL.y = eyeY * resolution_;
    face->eyeL.width = eyeW * resolution_;
    face->eyeL.height = eyeH * resolution_;
    face->eyeL.angle = 360.0f - eyeA;
    face->eyeL.origin = MII_ORIGIN_LEFT;

    face->eyebrowR.x = resolution_ * (32.0f - eyebrowX);
    face->eyebrowR.y = eyebrowY * resolution_;
    face->eyebrowR.width = eyebrowW * resolution_;
    face->eyebrowR.height = eyebrowH * resolution_;
    face->eyebrowR.angle = eyebrowA;
    face->eyebrowR.origin = MII_ORIGIN_RIGHT;

    face->eyebrowL.x = resolution_ * (32.0f + eyebrowX);
    face->eyebrowL.y = eyebrowY * resolution_;
    face->eyebrowL.width = eyebrowW * resolution_;
    face->eyebrowL.height = eyebrowH * resolution_;
    face->eyebrowL.angle = 360.0f - eyebrowA;
    face->eyebrowL.origin = MII_ORIGIN_LEFT;

    face->mouth.x = 32.0f * resolution_;
    face->mouth.y = mouthY * resolution_;
    face->mouth.width = mouthW * resolution_;
    face->mouth.height = mouthH * resolution_;
    face->mouth.angle = 0.0f;
    face->mouth.origin = MII_ORIGIN_CENTER;

    face->mustacheR.x = 32.0f * resolution_;
    face->mustacheR.y = mustacheY * resolution_;
    face->mustacheR.width = mustacheW * resolution_;
    face->mustacheR.height = mustacheH * resolution_;
    face->mustacheR.angle = 0.0f;
    face->mustacheR.origin = MII_ORIGIN_RIGHT;

    face->mustacheL.x = 32.0f * resolution_;
    face->mustacheL.y = mustacheY * resolution_;
    face->mustacheL.width = mustacheW * resolution_;
    face->mustacheL.height = mustacheH * resolution_;
    face->mustacheL.angle = 0.0f;
    face->mustacheL.origin = MII_ORIGIN_LEFT;

    face->mole.x = moleX * resolution_;
    face->mole.y = moleY * resolution_;
    face->mole.width = moleW * resolution_;
    face->mole.height = moleH * resolution_;
    face->mole.angle = 0.0f;
    face->mole.origin = MII_ORIGIN_CENTER;
}

void Mii_Capture(u8* buffer, const Mii_CharInfo* info, Mii_FaceParts* face, MII_I_MASKRSL resolution) {
    f32 vp[6];

    J3DNativeViewGetViewportv(vp);
    Mii_Setup2DCameraAndParam((f32)resolution);

    J3DPassBlend(NEBULA_FACTOR_INV_DST_ALPHA, NEBULA_FACTOR_DST_ALPHA);
    J3DPassWriteColor(1);

    Mii_SetStage4Mustache(info->beard.color);
    Mii_DrawFaceParts(&face->mustacheR);
    Mii_DrawFaceParts(&face->mustacheL);

    Mii_SetStage4Mouth(info->mouth.color);
    Mii_DrawFaceParts(&face->mouth);

    Mii_SetStage4Eyebrow(info->eyebrow.color);
    Mii_DrawFaceParts(&face->eyebrowR);
    Mii_DrawFaceParts(&face->eyebrowL);

    Mii_SetStage4Eye(info->eye.color, info->eye.type);
    Mii_DrawFaceParts(&face->eyeR);
    Mii_DrawFaceParts(&face->eyeL);

    Mii_SetStage4Mole();
    Mii_DrawFaceParts(&face->mole);

    // The parts' colours stay; their alpha is drawn again on a cleared alpha channel.
    J3DPassWriteColor(0);
    J3DNativeTargetClear(0x00000000, 2u | 4u);
    J3DPassBlend(NEBULA_FACTOR_INV_DST_ALPHA, NEBULA_FACTOR_ONE);

    Mii_SetStage4Mustache(info->beard.color);
    Mii_DrawFaceParts(&face->mustacheR);
    Mii_DrawFaceParts(&face->mustacheL);

    Mii_SetStage4Mouth(info->mouth.color);
    Mii_DrawFaceParts(&face->mouth);

    Mii_SetStage4Eyebrow(info->eyebrow.color);
    Mii_DrawFaceParts(&face->eyebrowR);
    Mii_DrawFaceParts(&face->eyebrowL);

    Mii_SetStage4Eye(info->eye.color, info->eye.type);
    Mii_DrawFaceParts(&face->eyeR);
    Mii_DrawFaceParts(&face->eyeL);

    Mii_SetStage4Mole();
    Mii_DrawFaceParts(&face->mole);

    J3DPassWriteColor(1);
    J3DNativeViewSetViewport(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
}

void Mii_DrawFaceParts(Mii_Part* part) {
    Mii_NativeTexturePart(&part->ngtobj.tobj);
    Mii_DrawQuad(part->x, part->y, part->width, part->height, part->angle, part->origin);
}

void Mii_DrawQuad(f32 x, f32 y, f32 width, f32 height, f32 rotZ, MII_ORIGIN origin) {
    Mtx rot;
    Mtx pos;
    f32 baseX;
    s16 s0;
    s16 s1;

    NebulaMtxIdentity(pos);
    NebulaMtxScaleApply(pos, pos, width, height, 1.0f);
    NebulaMtxRotRad(rot, 'z', (3.141592653589793f / 180.0f) * rotZ);
    NebulaMtxConcat(rot, pos, pos);
    NebulaMtxScaleApply(pos, pos, MII_I_TEX_SCALE_X, MII_I_TEX_SCALE_Y, 1.0f);
    NebulaMtxTransApply(pos, pos, x, y, 0.0f);

    J3DNativeShapeSetPosition(0, pos);
    J3DPassPositionMatrix(0);

    switch (origin) {
    case MII_ORIGIN_CENTER:
        baseX = -0.5f;
        s0 = 256;
        s1 = 0;
        break;
    case MII_ORIGIN_RIGHT:
        baseX = -1.0f;
        s0 = 256;
        s1 = 0;
        break;
    case MII_ORIGIN_LEFT:
        baseX = 0.0f;
        s0 = 0;
        s1 = 256;
        break;
    }

    J3DNativeDrawBegin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
    {
        J3DNativeDrawPosition(1.0 + baseX, -0.5f, 0.0f);
        J3DNativeDrawColor(0x000000FF);
        J3DNativeDrawTexCoord((s0) / 256.0f, (0) / 256.0f);

        J3DNativeDrawPosition(1.0 + baseX, 0.5f, 0.0f);
        J3DNativeDrawColor(0x000000FF);
        J3DNativeDrawTexCoord((s0) / 256.0f, (256) / 256.0f);

        J3DNativeDrawPosition(baseX, 0.5f, 0.0f);
        J3DNativeDrawColor(0x000000FF);
        J3DNativeDrawTexCoord((s1) / 256.0f, (256) / 256.0f);

        J3DNativeDrawPosition(baseX, -0.5f, 0.0f);
        J3DNativeDrawColor(0x000000FF);
        J3DNativeDrawTexCoord((s1) / 256.0f, (0) / 256.0f);
    }
}

MII_I_MASKRSL Mii_GetMaxMaskRsl(MiiResolution resolution) {
    switch (resolution) {
    case MiiResolution_64:
    case MiiResolution_64M:
        return MII_I_MASKRSL_64;
    case MiiResolution_128:
    case MiiResolution_128M:
        return MII_I_MASKRSL_128;
    case MiiResolution_256:
    case MiiResolution_256M:
        return MII_I_MASKRSL_256;
    default:
        return MII_I_MASKRSL_64;
    }
}

u32 Mii_GetMaskSize(MII_I_MASKRSL resolution) {
    return 2 * (resolution * resolution);
}

u32 Mii_GetMaskBufSize(MiiResolution resolution) {
    u32 size = 0;

    if (resolution & 32) {
        size += Mii_GetMaskSize(MII_I_MASKRSL_32);
    }

    if (resolution & 64) {
        size += Mii_GetMaskSize(MII_I_MASKRSL_64);
    }

    if (resolution & 128) {
        size += Mii_GetMaskSize(MII_I_MASKRSL_128);
    }

    if (resolution & 256) {
        size += Mii_GetMaskSize(MII_I_MASKRSL_256);
    }

    return size;
}
