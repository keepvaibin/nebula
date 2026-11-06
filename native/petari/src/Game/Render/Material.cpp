#include "Game/Render/Material.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/Transform.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_format.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

CoordMatrix::CoordMatrix() {
    memset(&mInfo, 0, sizeof(mInfo));
    mInfo.mProjection = 1;
    mInfo.mSRT.mScaleX = 1.0f;
    mInfo.mSRT.mScaleY = 1.0f;
    NebulaMtx44Identity(mInfo.mEffectMtx);
    NebulaMtxIdentity(mMtx);
}

void CoordMatrix::setEffectMtx(const Mtx mtx) {
    for (u32 r = 0; r < 3; r++) {
        for (u32 c = 0; c < 4; c++) {
            mInfo.mEffectMtx[r][c] = mtx[r][c];
        }
    }
    mInfo.mEffectMtx[3][0] = 0.0f;
    mInfo.mEffectMtx[3][1] = 0.0f;
    mInfo.mEffectMtx[3][2] = 0.0f;
    mInfo.mEffectMtx[3][3] = 1.0f;
}

// Maps of the world read the vertex stage's world rows: their own matrix starts from the world, not the eye.
void CoordMatrix::calc() {
    Mtx44 mtx1;
    Mtx44 mtx2;

    const Mtx qMtx = {
        {0.5f, 0.0f, 0.5f, 0.0f}, {0.0f, -0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
    };
    const Mtx qMtx2 = {
        {0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f},
    };

    u32 r29 = mInfo.mInfo & 0x3f;
    u32 r30 = (mInfo.mInfo >> 7) & 1;
    switch (r29) {
    case 8:
    case 11:
        if (r30 == 0) {
            textureMtx(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMaya(mInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx, mtx2);
        projConcat(mtx2, mInfo.mEffectMtx, mtx1);
        NebulaMtxCopy(mtx1, mMtx);
        break;
    case 9:
        if (r30 == 0) {
            textureMtx(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMaya(mInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx, mtx2);
        projConcat(mtx2, mInfo.mEffectMtx, mMtx);
        break;
    case 7:
        if (r30 == 0) {
            textureMtx(mInfo.mSRT, mInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            textureMtxMaya(mInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx, mMtx);
        break;
    case 10:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx2, mtx2);
        projConcat(mtx2, mInfo.mEffectMtx, mtx1);
        NebulaMtxCopy(mtx1, mMtx);
        break;
    case 6:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx2, mMtx);
        break;
    case 1:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mMtx);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mMtx);
        }
        break;
    case 2:
    case 5:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mtx2);
        }
        projConcat(mtx2, mInfo.mEffectMtx, mtx1);
        NebulaMtxCopy(mtx1, mMtx);
        break;
    case 3:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mtx2);
        }
        projConcat(mtx2, mInfo.mEffectMtx, mMtx);
        break;
    case 4:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mtx2);
        }
        projConcat(mtx2, mInfo.mEffectMtx, mMtx);
        break;
    default:
        if (r30 == 0) {
            textureMtxOld(mInfo.mSRT, mInfo.mCenter, mMtx);
        } else if (r30 == 1) {
            textureMtxMayaOld(mInfo.mSRT, mMtx);
        }
        break;
    }
}

namespace {
    f32 sAmbient[2][4];

    void setColor(f32* pOut, NebulaColor8 color) {
        pOut[0] = color.r / 255.0f;
        pOut[1] = color.g / 255.0f;
        pOut[2] = color.b / 255.0f;
        pOut[3] = color.a / 255.0f;
    }
}  // namespace

namespace MaterialAmbient {
    void set(u32 channel, NebulaColor8 color) {
        setColor(sAmbient[channel & 1], color);
    }

    const f32* get(u32 channel) {
        return sAmbient[channel & 1];
    }
}  // namespace MaterialAmbient

void Material::init(ModelData* pData, u32 index) {
    const NebulaModelMaterial* pFile = reinterpret_cast< const NebulaModelMaterial* >(pData->at(pData->mFile->materials_at)) + index;
    mFile = pFile;
    mData = pData;
    mNext = nullptr;
    mMesh = pFile->mesh != NEBULA_MODEL_NONE && pData->mMeshes != nullptr ? &pData->mMeshes[pFile->mesh] : nullptr;
    mJoint = pFile->joint != NEBULA_MODEL_NONE && pData->mJoints != nullptr ? &pData->mJoints[pFile->joint] : nullptr;
    mIndex = index;
    mMatrixUsed = 0;
    mStamp = 0;
    mTextureStamp = 0;
    mForms = nullptr;
    mSourceHash = pFile->name_hash;
    mFollowsView = false;

    const u32* pTextures = reinterpret_cast< const u32* >(pData->at(pFile->textures_at));
    for (u32 i = 0; i < NEBULA_MATERIAL_SLOTS; i++) {
        mTexNo[i] = i < pFile->texture_count ? pTextures[i] : NEBULA_NONE32;
    }
    for (u32 i = 0; i < 2; i++) {
        memcpy(&mMatColor[i], pFile->material_color[i], 4);
        memcpy(&mAmbColor[i], pFile->ambient_color[i], 4);
    }
    for (u32 i = 0; i < 4; i++) {
        mRegisters[i].r = pFile->registers[i][0];
        mRegisters[i].g = pFile->registers[i][1];
        mRegisters[i].b = pFile->registers[i][2];
        mRegisters[i].a = pFile->registers[i][3];
        memcpy(&mConstants[i], pFile->constants[i], 4);
    }
    mFog.mType = pFile->fog.type;
    mFog.mStartZ = pFile->fog.start;
    mFog.mEndZ = pFile->fog.end;
    mFog.mNearZ = pFile->fog.near_z;
    mFog.mFarZ = pFile->fog.far_z;
    memcpy(&mFog.mColor, pFile->fog.color, 4);

    const NebulaModelCoord* pCoords = reinterpret_cast< const NebulaModelCoord* >(pData->at(pFile->coords_at));
    for (u32 i = 0; i < NEBULA_MATERIAL_SLOTS; i++) {
        mCoords[i].mType = i < pFile->coord_count ? pCoords[i].type : 0;
        mCoords[i].mSource = i < pFile->coord_count ? pCoords[i].source : 0;
        mCoords[i].mMatrix = i < pFile->coord_count ? pCoords[i].matrix : NEBULA_COORD_IDENTITY;
    }
    const NebulaModelCoordMatrix* pMatrices = reinterpret_cast< const NebulaModelCoordMatrix* >(pData->at(pFile->matrices_at));
    for (u32 i = 0; i < pFile->matrix_count; i++) {
        const NebulaModelCoordMatrix& rFile = pMatrices[i];
        CoordMatrix& rMatrix = mMatrices[rFile.slot];
        rMatrix.mInfo.mProjection = rFile.projection;
        rMatrix.mInfo.mInfo = rFile.info;
        rMatrix.mInfo.mCenter.x = rFile.center[0];
        rMatrix.mInfo.mCenter.y = rFile.center[1];
        rMatrix.mInfo.mCenter.z = rFile.center[2];
        rMatrix.mInfo.mSRT.mScaleX = rFile.scale[0];
        rMatrix.mInfo.mSRT.mScaleY = rFile.scale[1];
        rMatrix.mInfo.mSRT.mRotation = rFile.rotation;
        rMatrix.mInfo.mSRT.mTranslationX = rFile.translation[0];
        rMatrix.mInfo.mSRT.mTranslationY = rFile.translation[1];
        memcpy(rMatrix.mInfo.mEffectMtx, rFile.effect, sizeof(rFile.effect));
        mMatrixUsed |= static_cast< u16 >(1u << rFile.slot);
        mFollowsView = mFollowsView || rMatrix.followsView() || ((pFile->flags & NEBULA_MATERIAL_ON_BILLBOARD) != 0 && rMatrix.readsEye());
    }
    calc();
}

void Material::calc() {
    bool changed = false;
    for (u32 i = 0; i < NEBULA_MATERIAL_SLOTS; i++) {
        if ((mMatrixUsed & (1u << i)) != 0) {
            Mtx before;
            NebulaMtxCopy(mMatrices[i].getMtx(), before);
            mMatrices[i].calc();
            changed = changed || memcmp(before, mMatrices[i].getMtx(), sizeof(Mtx)) != 0;
        }
    }
    if (changed) {
        touch();
    }
}

s32 Material::findForm(const FormKey& rKey) const {
    const NebulaModelForm* pForms = reinterpret_cast< const NebulaModelForm* >(mData->at(mFile->forms_at));
    const u8 flags = static_cast< u8 >((rKey.mNormalCoord ? NEBULA_FORM_NORMAL_COORD : 0) | (rKey.mLit ? NEBULA_FORM_LIT : 0) |
                                       (rKey.mFogLinear ? NEBULA_FORM_FOG_LINEAR : 0));
    const u8 mask = NEBULA_FORM_NORMAL_COORD | NEBULA_FORM_LIT | NEBULA_FORM_FOG_LINEAR;
    for (u32 i = 0; i < mFile->form_count; i++) {
        const NebulaModelForm& rForm = pForms[i];
        if (rForm.replace == rKey.mReplace && rForm.pass == rKey.mPass && (rForm.flags & mask) == flags) {
            return static_cast< s32 >(i);
        }
    }
    return -1;
}

const NebulaModelForm* Material::getForm(s32 index) const {
    return reinterpret_cast< const NebulaModelForm* >(mData->at(mFile->forms_at)) + index;
}

void Material::fillRow(u32 row, f32* pOut) const {
    const bool stageColors = (mFile->flags & NEBULA_MATERIAL_HAS_STAGE_COLORS) != 0;
    if (row >= NEBULA_ROW_COORD) {
        const u32 at = row - NEBULA_ROW_COORD;
        const u32 coord = at / 3;
        const u32 r = at % 3;
        pOut[0] = r == 0 ? 1.0f : 0.0f;
        pOut[1] = r == 1 ? 1.0f : 0.0f;
        pOut[2] = r == 2 ? 1.0f : 0.0f;
        pOut[3] = 0.0f;
        const u32 place = mCoords[coord].mMatrix;
        if (place != NEBULA_COORD_IDENTITY) {
            const u32 number = place;
            if ((mMatrixUsed & (1u << number)) != 0) {
                memcpy(pOut, const_cast< CoordMatrix& >(mMatrices[number]).getMtx()[r], sizeof(f32) * 4);
                if ((mFile->flags & NEBULA_MATERIAL_ON_BILLBOARD) != 0 && mMatrices[number].readsEye()) {
                    // A billboard's eye rows have turned to the eye; the coordinate reads the joint before that: world
                    // input, under this view. A direction takes no translation of it.
                    const f32 (*pView)[4] = ModelSys::mViewMtx;
                    const f32 a0 = pOut[0], a1 = pOut[1], a2 = pOut[2];
                    for (u32 c = 0; c < 3; c++) {
                        pOut[c] = a0 * pView[0][c] + a1 * pView[1][c] + a2 * pView[2][c];
                    }
                    if (mCoords[coord].mSource == 0) {
                        pOut[3] += a0 * pView[0][3] + a1 * pView[1][3] + a2 * pView[2][3];
                    }
                }
            }
        }
        return;
    }
    if (row >= NEBULA_ROW_WARP) {
        const u32 at = row - NEBULA_ROW_WARP;
        const u32 warp = at / 2;
        const u32 r = at % 2;
        pOut[3] = 0.0f;
        if ((mFile->flags & NEBULA_MATERIAL_HAS_INDIRECT) == 0) {
            pOut[0] = r == 0 ? 1.0f : 0.0f;
            pOut[1] = r == 1 ? 1.0f : 0.0f;
            pOut[2] = 0.0f;
            return;
        }
        if (warp >= mFile->warp_count) {
            pOut[0] = pOut[1] = pOut[2] = 0.0f;
            return;
        }
        const NebulaModelWarp& rWarp = reinterpret_cast< const NebulaModelWarp* >(mData->at(mFile->warps_at))[warp];
        for (u32 c = 0; c < 3; c++) {
            pOut[c] = rWarp.rows[r][c];
        }
        return;
    }
    if (row >= NEBULA_ROW_SCALE) {
        const u32 coord = row - NEBULA_ROW_SCALE;
        const auto sizeOf = [this](u32 map, f32* pSize) {
            const u32 number = map < NEBULA_MATERIAL_SLOTS ? mTexNo[map] : NEBULA_NONE32;
            if (number == NEBULA_NONE32) {
                pSize[0] = pSize[1] = 1.0f;
            } else {
                const ResTIMG* pTexture = mData->getTexture(number);
                pSize[0] = pTexture->mWidth;
                pSize[1] = pTexture->mHeight;
            }
        };
        sizeOf(coord, pOut + 2);
        const u8 map = mFile->coord_map[coord];
        if (map != 255) {
            sizeOf(map, pOut);
        } else {
            pOut[0] = pOut[2];
            pOut[1] = pOut[3];
        }
        return;
    }
    if (row == NEBULA_ROW_OVERRIDE) {
        pOut[0] = pOut[1] = pOut[2] = pOut[3] = 0.0f;
        return;
    }
    if (row == NEBULA_ROW_ALPHA_REF) {
        pOut[0] = mFile->alpha_ref0 / 255.0f;
        pOut[1] = mFile->alpha_ref1 / 255.0f;
        pOut[2] = 0.0f;
        pOut[3] = 0.0f;
        return;
    }
    if (row == NEBULA_ROW_FOG_COLOR) {
        const bool has = (mFile->flags & NEBULA_MATERIAL_HAS_FOG) != 0;
        pOut[0] = has ? mFog.mColor.r / 255.0f : 0.0f;
        pOut[1] = has ? mFog.mColor.g / 255.0f : 0.0f;
        pOut[2] = has ? mFog.mColor.b / 255.0f : 0.0f;
        pOut[3] = has ? static_cast< f32 >(mFog.mType) : 0.0f;
        return;
    }
    if (row == NEBULA_ROW_FOG) {
        const bool has = (mFile->flags & NEBULA_MATERIAL_HAS_FOG) != 0;
        pOut[0] = has ? mFog.mStartZ : 0.0f;
        pOut[1] = has ? mFog.mEndZ : 0.0f;
        pOut[2] = has ? mFog.mNearZ : 0.0f;
        pOut[3] = has ? mFog.mFarZ : 0.0f;
        return;
    }
    if (row >= NEBULA_ROW_AMBIENT_COLOR) {
        memcpy(pOut, MaterialAmbient::get(row - NEBULA_ROW_AMBIENT_COLOR), sizeof(f32) * 4);
        return;
    }
    if (row >= NEBULA_ROW_MATERIAL_COLOR) {
        setColor(pOut, mMatColor[row - NEBULA_ROW_MATERIAL_COLOR]);
        return;
    }
    if (row >= NEBULA_ROW_CONSTANT) {
        if (stageColors) {
            setColor(pOut, mConstants[row - NEBULA_ROW_CONSTANT]);
        } else {
            pOut[0] = pOut[1] = pOut[2] = pOut[3] = 1.0f;
        }
        return;
    }
    if (stageColors) {
        const NebulaColor16& rRegister = mRegisters[row - NEBULA_ROW_REGISTER];
        pOut[0] = rRegister.r / 255.0f;
        pOut[1] = rRegister.g / 255.0f;
        pOut[2] = rRegister.b / 255.0f;
        pOut[3] = rRegister.a / 255.0f;
    } else {
        pOut[0] = pOut[1] = pOut[2] = pOut[3] = 1.0f;
    }
}

void Material::fillRows(const NebulaModelForm& rForm, f32 (*pOut)[4]) const {
    const u32* pVertex = reinterpret_cast< const u32* >(mData->at(rForm.vertex_rows_at));
    for (u32 i = 0; i < rForm.vertex_row_count; i++) {
        fillRow(pVertex[i], *pOut++);
    }
    const u32* pPixel = reinterpret_cast< const u32* >(mData->at(rForm.pixel_rows_at));
    for (u32 i = 0; i < rForm.pixel_row_count; i++) {
        fillRow(pPixel[i], *pOut++);
    }
}

Material::FormState* Material::formState(s32 index) {
    if (mForms == nullptr) {
        mForms = new FormState[mFile->form_count];
        memset(mForms, 0, sizeof(FormState) * mFile->form_count);
        for (u32 i = 0; i < mFile->form_count; i++) {
            mForms[i].mSlot = NEBULA_NONE32;
            mForms[i].mTable = NEBULA_NONE32;
            mForms[i].mPipeline = NEBULA_NONE32;
            mForms[i].mConstantsFrame = NEBULA_NONE32;
        }
    }
    return &mForms[index];
}

MaterialDraw::MaterialDraw() {
    clear();
}

MaterialDraw::MaterialDraw(Material* pMaterial) {
    send(pMaterial);
}

void MaterialDraw::clear() {
    mMaterial = nullptr;
    mOwn = nullptr;
    mKey.mReplace = 0;
    mKey.mPass = 0;
    mKey.mNormalCoord = false;
    mKey.mFogLinear = false;
    mKey.mLit = false;
    mFormLit = false;
    mEdits = 0;
    mHasOverride = false;
    mHasState = false;
    mZeroAlphaRef = false;
    mState = 0;
    mImageMask = 0;
    mCoordinateTexture = false;
    mFlat = false;
    mForm = -1;
    mOverride[0] = mOverride[1] = mOverride[2] = mOverride[3] = 0.0f;
}

void MaterialDraw::send(Material* pMaterial) {
    clear();
    mMaterial = pMaterial;
    if (pMaterial->hasAmbient()) {
        MaterialAmbient::set(0, pMaterial->mAmbColor[0]);
        MaterialAmbient::set(1, pMaterial->mAmbColor[1]);
    }
}

void MaterialDraw::sendOwn(Material* pMaterial, const Material* pShared) {
    mOwn = pMaterial != nullptr && pMaterial != pShared ? pMaterial : nullptr;
}

void MaterialDraw::override(u32 mode, f32 a0, f32 a1, f32 ref) {
    mKey.mReplace = static_cast< u8 >(mode);
    mHasOverride = true;
    mOverride[0] = static_cast< f32 >(mode);
    mOverride[1] = a0;
    mOverride[2] = a1;
    mOverride[3] = ref;
    mForm = -1;
}

void MaterialDraw::overrideAlpha(f32 alpha) {
    if (mOverride[0] == 5.0f) {
        mOverride[1] = alpha;
    }
}

void MaterialDraw::overrideCoordinate(u32 texture, u32 width, u32 height) {
    mKey.mNormalCoord = true;
    mForm = -1;
    mScale0[0] = mScale0[2] = width;
    mScale0[1] = mScale0[3] = height;
    mEdits |= Edit_Scale0;
    mImageMask |= 1;
    mImages[0] = texture;
    mCoordinateTexture = true;
}

void MaterialDraw::swapImage(u32 map, u32 texture) {
    mImageMask |= 1u << map;
    mImages[map] = texture;
}

void MaterialDraw::applyPass(u8 pass) {
    mKey.mPass |= pass;
    if ((pass & NEBULA_FORM_PASS_PLAYER_OPACITY) != 0) {
        mZeroAlphaRef = true;
    }
    mForm = -1;
}

void MaterialDraw::setCoordinate(u32 coord, const f32 (*pMtx)[4]) {
    memcpy(mCoords[coord], pMtx, sizeof(mCoords[coord]));
    mEdits |= Edit_Coord0 << coord;
}

void MaterialDraw::setRegister(u32 index, NebulaColor8 color) {
    if ((index & 3) != 0) {
        setColor(mRegisters[(index & 3) - 1], color);
        mEdits |= Edit_Register1 << ((index & 3) - 1);
    }
}

const f32* MaterialDraw::getRegister(u32 index) {
    const u32 number = (index & 3) - 1;
    if ((mEdits & (Edit_Register1 << number)) == 0) {
        getMaterial()->fillRow(static_cast< u32 >(NEBULA_ROW_REGISTER + number), mRegisters[number]);
        mEdits |= Edit_Register1 << number;
    }
    return mRegisters[number];
}

void MaterialDraw::setMaterialColor(u32 channel, NebulaColor8 color) {
    setColor(mMaterialColors[channel & 1], color);
    mEdits |= Edit_MaterialColor0 << (channel & 1);
}

void MaterialDraw::setAmbient(u32 channel, NebulaColor8 color) {
    setColor(mAmbient[channel & 1], color);
    mEdits |= Edit_Ambient0 << (channel & 1);
}

void MaterialDraw::setFog(f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
    mFogValues[0][0] = start;
    mFogValues[0][1] = end;
    mFogValues[0][2] = nearZ;
    mFogValues[0][3] = farZ;
    mFogValues[1][0] = color.r / 255.0f;
    mFogValues[1][1] = color.g / 255.0f;
    mFogValues[1][2] = color.b / 255.0f;
    mFogValues[1][3] = 2.0f;
    mEdits |= Edit_Fog;
    mKey.mFogLinear = (mMaterial->mFile->flags & NEBULA_MATERIAL_HAS_FOG) == 0;
    mForm = -1;
}

void MaterialDraw::setState(u32 state) {
    mState = state;
    mHasState = true;
}

u32 MaterialDraw::getState() const {
    return mHasState ? mState : mMaterial->mFile->state;
}

void MaterialDraw::setFlatColor(f32 r, f32 g, f32 b, f32 a) {
    mFlat = true;
    mFlatColor[0] = r;
    mFlatColor[1] = g;
    mFlatColor[2] = b;
    mFlatColor[3] = a;
}

s32 MaterialDraw::resolveForm() {
    Material* pMaterial = getMaterial();
    mKey.mLit = Frame::pixelLit();
    s32 form = pMaterial->findForm(mKey);
    if (form < 0 && mKey.mLit) {
        mKey.mLit = false;
        form = pMaterial->findForm(mKey);
    }
    if (form < 0 && (mKey.mPass & NEBULA_FORM_PASS_FLEXIBLE_COORDINATES) != 0) {
        // A model a mod made for one object used by another: drawn as it is, not lost.
        mKey.mPass &= static_cast< u8 >(~NEBULA_FORM_PASS_FLEXIBLE_COORDINATES);
        form = pMaterial->findForm(mKey);
    }
    return form;
}

void MaterialDraw::applyEdits(const u32* pRows, u32 count, f32 (*pOut)[4]) const {
    for (u32 i = 0; i < count; i++) {
        const u32 row = pRows[i];
        f32* pRow = pOut[i];
        if (row >= NEBULA_ROW_COORD) {
            const u32 at = row - NEBULA_ROW_COORD;
            if ((mEdits & (Edit_Coord0 << (at / 3))) != 0) {
                memcpy(pRow, mCoords[at / 3][at % 3], sizeof(f32) * 4);
            }
        } else if (row == NEBULA_ROW_SCALE) {
            if ((mEdits & Edit_Scale0) != 0) {
                memcpy(pRow, mScale0, sizeof(mScale0));
            }
        } else if (row == NEBULA_ROW_OVERRIDE) {
            memcpy(pRow, mOverride, sizeof(mOverride));
        } else if (row == NEBULA_ROW_ALPHA_REF) {
            if (mZeroAlphaRef) {
                pRow[0] = pRow[1] = 0.0f;
            }
        } else if (row == NEBULA_ROW_FOG || row == NEBULA_ROW_FOG_COLOR) {
            if ((mEdits & Edit_Fog) != 0) {
                memcpy(pRow, mFogValues[row - NEBULA_ROW_FOG], sizeof(f32) * 4);
            }
        } else if (row >= NEBULA_ROW_AMBIENT_COLOR && row < NEBULA_ROW_AMBIENT_COLOR + 2) {
            if ((mEdits & (Edit_Ambient0 << (row - NEBULA_ROW_AMBIENT_COLOR))) != 0) {
                memcpy(pRow, mAmbient[row - NEBULA_ROW_AMBIENT_COLOR], sizeof(f32) * 4);
            }
        } else if (row >= NEBULA_ROW_MATERIAL_COLOR && row < NEBULA_ROW_MATERIAL_COLOR + 2) {
            if ((mEdits & (Edit_MaterialColor0 << (row - NEBULA_ROW_MATERIAL_COLOR))) != 0) {
                memcpy(pRow, mMaterialColors[row - NEBULA_ROW_MATERIAL_COLOR], sizeof(f32) * 4);
            }
        } else if (row >= NEBULA_ROW_REGISTER && row < NEBULA_ROW_REGISTER + 3) {
            if ((mEdits & (Edit_Register1 << (row - NEBULA_ROW_REGISTER))) != 0) {
                memcpy(pRow, mRegisters[row - NEBULA_ROW_REGISTER], sizeof(f32) * 4);
            }
        }
    }
}

namespace {
    // Which vertex rows begin a coordinate matrix: between two game frames these move as matrices, the rest mix.
    u32* coordRowsOf(const u32* pRows, u32 count, u32* pCount) {
        *pCount = 0;
        for (u32 i = 0; i < count; ++i) {
            if (pRows[i] >= NEBULA_ROW_COORD && (pRows[i] - NEBULA_ROW_COORD) % 3 == 0) { ++*pCount; }
        }
        u32* rows = *pCount != 0 ? new u32[*pCount] : nullptr;
        u32 next = 0;
        for (u32 i = 0; i < count; ++i) {
            if (pRows[i] >= NEBULA_ROW_COORD && (pRows[i] - NEBULA_ROW_COORD) % 3 == 0) { rows[next++] = i; }
        }
        return rows;
    }

    bool readsAmbient(const NebulaModelForm& rForm, const ModelData* pData) {
        const u32* pPixel = reinterpret_cast< const u32* >(pData->at(rForm.pixel_rows_at));
        for (u32 i = 0; i < rForm.pixel_row_count; i++) {
            if (pPixel[i] == NEBULA_ROW_AMBIENT_COLOR || pPixel[i] == NEBULA_ROW_AMBIENT_COLOR + 1) {
                return true;
            }
        }
        const u32* pVertex = reinterpret_cast< const u32* >(pData->at(rForm.vertex_rows_at));
        for (u32 i = 0; i < rForm.vertex_row_count; i++) {
            if (pVertex[i] == NEBULA_ROW_AMBIENT_COLOR || pVertex[i] == NEBULA_ROW_AMBIENT_COLOR + 1) {
                return true;
            }
        }
        return false;
    }
}  // namespace

u32 MaterialDraw::constantsOf(s32 formIndex, const NebulaModelForm& rForm) {
    Material* pMaterial = getMaterial();
    Material::FormState* pState = pMaterial->formState(formIndex);
    const u32 rowCount = rForm.vertex_row_count + rForm.pixel_row_count;
    const bool plain = mEdits == 0 && !mHasOverride && !mZeroAlphaRef;

    if (!pState->mAnalysed) {
        pState->mAnalysed = true;
        pState->mReadsAmbient = readsAmbient(rForm, pMaterial->mData);
        pState->mCoordRows = coordRowsOf(reinterpret_cast< const u32* >(pMaterial->mData->at(rForm.vertex_rows_at)), rForm.vertex_row_count, &pState->mCoordRowCount);
    }

    if (pMaterial->mFollowsView) {
        // The effect matrices these maps hold are the game's to change at any time.
        pMaterial->calc();
    }

    // A material nothing moves is one registered slot, for as long as it stays as it was registered.
    if (plain && mOwn == nullptr && !pState->mReadsAmbient && !pMaterial->mFollowsView &&
        (pState->mSlot == NEBULA_NONE32 || pState->mSlotStamp == pMaterial->mStamp)) {
        if (pState->mSlot == NEBULA_NONE32) {
            f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(rowCount * 16, 16));
            pMaterial->fillRows(rForm, pRows);
            const NebulaHostServices* pHost = nebula_host();
            pState->mSlot = pHost->material_register(pHost->user, pRows, rForm.vertex_row_count, rForm.pixel_row_count);
            pState->mSlotStamp = pMaterial->mStamp;
        }
        return Frame::addConstants(pState->mSlot, rForm.vertex_row_count, rForm.pixel_row_count, nullptr, pState->mCoordRows, pState->mCoordRowCount);
    }

    // Moved or edited: rows in the frame, once a frame while they stay the same.
    if (plain && pState->mConstantsFrame == Frame::number() && pState->mConstantsStamp == pMaterial->mStamp &&
        !pState->mReadsAmbient) {
        return pState->mConstantsIndex;
    }
    f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(rowCount * 16, 16));
    pMaterial->fillRows(rForm, pRows);
    if (!plain) {
        applyEdits(reinterpret_cast< const u32* >(pMaterial->mData->at(rForm.vertex_rows_at)), rForm.vertex_row_count, pRows);
        applyEdits(reinterpret_cast< const u32* >(pMaterial->mData->at(rForm.pixel_rows_at)), rForm.pixel_row_count, pRows + rForm.vertex_row_count);
    }
    const u32 index = Frame::addConstants(NEBULA_NONE32, rForm.vertex_row_count, rForm.pixel_row_count, pRows, pState->mCoordRows, pState->mCoordRowCount);
    if (plain) {
        pState->mConstantsFrame = Frame::number();
        pState->mConstantsStamp = pMaterial->mStamp;
        pState->mConstantsIndex = index;
    }
    return index;
}

u32 MaterialDraw::tableOf(s32 formIndex, const NebulaModelForm& rForm) {
    Material* pMaterial = getMaterial();
    Material::FormState* pState = pMaterial->formState(formIndex);
    ModelData* pData = pMaterial->mData;
    if (mImageMask == 0 && pState->mTable != NEBULA_NONE32 && pState->mTableStamp == pMaterial->mTextureStamp &&
        pState->mTableDataStamp == pData->getTextureStamp()) {
        return pState->mTable;
    }

    NebulaFrameTexture list[NEBULA_MATERIAL_SLOTS];
    const u8* pNumbers = reinterpret_cast< const u8* >(pData->at(rForm.textures_at));
    for (u32 i = 0; i < rForm.texture_count; i++) {
        const u32 map = pNumbers[i];
        NebulaFrameTexture& rOut = list[i];
        memset(&rOut, 0, sizeof(rOut));
        const u32 number = pMaterial->mTexNo[map];
        if (number != NEBULA_NONE32) {
            const ResTIMG* pTexture = pData->getTexture(number);
            rOut.texture = RenderTexture::idOf(pTexture->image());
            rOut.wrap_s = pTexture->mWrapS;
            rOut.wrap_t = pTexture->mWrapT;
            rOut.min_linear = pTexture->mMinType == 1 || pTexture->mMinType == 3 || pTexture->mMinType == 5;
            rOut.mag_linear = pTexture->mMagType != 0;
            rOut.mip_filter = pTexture->mMinType < 2 ? 0 : pTexture->mMinType < 4 ? 1 : 2;
            rOut.anisotropy = pTexture->mMaxAnisotropy;
            rOut.min_lod = pTexture->mMinLod;
            rOut.max_lod = pTexture->mMaxLod;
            rOut.lod_bias = pTexture->mLodBias;
        }
        if ((mImageMask & (1u << map)) != 0) {
            rOut.texture = mImages[map];
            if (map == 0 && mCoordinateTexture) {
                memset(&rOut, 0, sizeof(rOut));
                rOut.texture = mImages[0];
                rOut.wrap_s = 1;
                rOut.wrap_t = 1;
                rOut.min_linear = 1;
                rOut.mag_linear = 1;
            }
        }
    }
    const NebulaHostServices* pHost = nebula_host();
    const u32 table = pHost->texture_table(pHost->user, list, rForm.texture_count);
    if (mImageMask == 0) {
        pState->mTable = table;
        pState->mTableStamp = pMaterial->mTextureStamp;
        pState->mTableDataStamp = pData->getTextureStamp();
    }
    return table;
}

void meshLayoutInput(NebulaPipelineDesc* pDesc, u8 semantic, const NebulaModelMesh& rMesh) {
    {
        NebulaLayoutInput& rInput = pDesc->inputs[pDesc->input_count++];
        rInput.semantic = semantic;
        rInput.reserved = 0;
        const auto place = [&rInput](u8 stream, u8 format, u32 offset, u32 stride) {
            rInput.stream = stream;
            rInput.format = format;
            rInput.offset = offset;
            rInput.stride = stride;
        };
        const auto further = [&rMesh, &place](u8 format, u32 offset) {
            place(3, format, offset, rMesh.further_stride);
        };
        u32 coords = 0;
        for (u32 n = 1; n < NEBULA_MATERIAL_SLOTS; n++) {
            if ((rMesh.attributes & (NEBULA_MESH_COORD0 << n)) != 0) {
                coords++;
            }
        }
        const u32 afterCoords = static_cast< u32 >(coords * 8);
        const u32 afterColor = static_cast< u32 >(afterCoords + ((rMesh.attributes & NEBULA_MESH_COLOR1) != 0 ? 4 : 0));
        switch (semantic) {
        case NEBULA_INPUT_NORMAL:
            if ((rMesh.attributes & NEBULA_MESH_NORMAL) != 0) {
                place(1, 0, 0, 24);
                return;
            }
            break;
        case NEBULA_INPUT_COLOR0:
            if ((rMesh.attributes & NEBULA_MESH_COLOR0) != 0) {
                place(1, 2, 12, 24);
                return;
            }
            break;
        case NEBULA_INPUT_COLOR1:
            if ((rMesh.attributes & NEBULA_MESH_COLOR1) != 0) {
                further(2, afterCoords);
                return;
            }
            break;
        case NEBULA_INPUT_TANGENT:
            if ((rMesh.attributes & NEBULA_MESH_TANGENTS) != 0) {
                further(0, afterColor);
                return;
            }
            break;
        case NEBULA_INPUT_BINORMAL:
            if ((rMesh.attributes & NEBULA_MESH_TANGENTS) != 0) {
                further(0, static_cast< u32 >(afterColor + 12));
                return;
            }
            break;
        default: {
            const u32 number = semantic - NEBULA_INPUT_COORD0;
            if ((rMesh.attributes & (NEBULA_MESH_COORD0 << number)) != 0) {
                if (number == 0) {
                    place(1, 1, 16, 24);
                } else {
                    u32 offset = 0;
                    for (u32 n = 1; n < number; n++) {
                        if ((rMesh.attributes & (NEBULA_MESH_COORD0 << n)) != 0) {
                            offset = static_cast< u32 >(offset + 8);
                        }
                    }
                    further(1, offset);
                }
                return;
            }
            break;
        }
        }
        // What the mesh has none of reads zero.
        place(NEBULA_STREAM_ZERO, semantic >= NEBULA_INPUT_COORD0 ? 1 : semantic == NEBULA_INPUT_COLOR0 || semantic == NEBULA_INPUT_COLOR1 ? 2 : 0, 0, 0);
    }
}

u32 MaterialDraw::pipelineOf(s32 formIndex, const NebulaModelForm& rForm, const Mesh* pMesh) {
    Material* pMaterial = getMaterial();
    Material::FormState* pState = pMaterial->formState(formIndex);
    const u32 state = Frame::finishState(getState());
    const u32 attributes = pMesh->mFile->attributes;
    const u32 stride = pMesh->mFile->further_stride;
    if (pState->mPipeline != NEBULA_NONE32 && pState->mPipelineState == state && pState->mPipelineAttributes == attributes && pState->mPipelineStride == stride) {
        return pState->mPipeline;
    }
    NebulaPipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex = rForm.vertex;
    desc.pixel = rForm.pixel;
    desc.state = state;
    const u8* pInputs = reinterpret_cast< const u8* >(pMaterial->mData->at(rForm.inputs_at));
    u8 programCoord = 0;
    for (u32 i = 0; i < rForm.input_count; i++) {
        meshLayoutInput(&desc, pInputs[i], *pMesh->mFile);
        if (pInputs[i] >= NEBULA_INPUT_COORD0) {
            desc.inputs[desc.input_count - 1].semantic = static_cast< u8 >(NEBULA_INPUT_COORD0 + programCoord++);
        }
    }
    const NebulaHostServices* pHost = nebula_host();
    pState->mPipeline = pHost->pipeline_register(pHost->user, &desc);
    pState->mPipelineState = state;
    pState->mPipelineAttributes = attributes;
    pState->mPipelineStride = stride;
    return pState->mPipeline;
}

void MaterialDraw::bind(TypedDraw& rDraw) {
    Material* pMaterial = getMaterial();
    if (mForm < 0 || mFormLit != Frame::pixelLit()) {
        mFormLit = Frame::pixelLit();
        mForm = resolveForm();
        if (mForm < 0) {
            Frame::notDrawn(pMaterial->mData, pMaterial->mIndex, mKey.mReplace, mKey.mPass);
            return;
        }
    }
    const NebulaModelForm& rForm = *pMaterial->getForm(mForm);
    rDraw.setForm(rForm.vertex, rForm.pixel, reinterpret_cast< const u8* >(pMaterial->mData->at(rForm.inputs_at)), rForm.input_count,
                  constantsOf(mForm, rForm), tableOf(mForm, rForm), getState());
    rDraw.setSource(pMaterial->mSourceHash);
}

void MaterialDraw::draw(Model* pModel, Mesh* pMesh) {
    Material* pMaterial = getMaterial();
    if (mFlat) {
        drawFlat(pModel, pMesh);
        return;
    }
    if (mForm < 0 || mFormLit != Frame::pixelLit()) {
        mFormLit = Frame::pixelLit();
        mForm = resolveForm();
        if (mForm < 0) {
            Frame::notDrawn(pMaterial->mData, pMaterial->mIndex, mKey.mReplace, mKey.mPass);
            return;
        }
    }
    const NebulaModelForm& rForm = *pMaterial->getForm(mForm);
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = pMesh->mGpuMesh;
    rItem.pipeline = pipelineOf(mForm, rForm, pMesh);
    rItem.constants = constantsOf(mForm, rForm);
    rItem.textures = tableOf(mForm, rForm);
    rItem.block = pModel->block(pMaterial);
    rItem.slot = pModel->getSlot();
    rItem.generation = pModel->mMotionEpoch;
    rItem.part = pMesh->mIndex;
    rItem.source_hash = pMaterial->mSourceHash;
    rItem.matrix = pMesh->mFile->single_matrix != NEBULA_MODEL_NONE ? pMesh->mFile->single_matrix : 0;
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet();
    rItem.flags = static_cast< u32 >(mKey.mReplace != 0 || mKey.mPass != 0 ? NEBULA_ITEM_PLAIN_MESH : 0);
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}

void MaterialDraw::drawUnder(TypedDraw& rDraw, Mesh* pMesh) {
    Material* pMaterial = getMaterial();
    if (mForm < 0 || mFormLit != Frame::pixelLit()) {
        mFormLit = Frame::pixelLit();
        mForm = resolveForm();
        if (mForm < 0) {
            Frame::notDrawn(pMaterial->mData, pMaterial->mIndex, mKey.mReplace, mKey.mPass);
            return;
        }
    }
    const NebulaModelForm& rForm = *pMaterial->getForm(mForm);
    const u32 block = rDraw.currentBlock();
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = pMesh->mGpuMesh;
    rItem.pipeline = pipelineOf(mForm, rForm, pMesh);
    rItem.constants = constantsOf(mForm, rForm);
    rItem.textures = tableOf(mForm, rForm);
    rItem.block = block;
    rItem.slot = Frame::objectSlot();
    rItem.generation = Frame::objectGeneration();
    rItem.part = Frame::nextPart();
    rItem.source_hash = pMaterial->mSourceHash;
    rItem.matrix = 0;
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet();
    rItem.flags = NEBULA_ITEM_PLAIN_MESH | NEBULA_ITEM_ONE_MATRIX;
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}

void MaterialDraw::drawFlat(Model* pModel, Mesh* pMesh) {
    NebulaFrameItem& rItem = Frame::addItem();
    rItem.mesh = pMesh->mGpuMesh;
    rItem.pipeline = Frame::sitePipeline(NEBULA_SHADER_FLAT_COLOR, Frame::finishState(getState()), pMesh->mFile);
    f32 (*pRows)[4] = static_cast< f32 (*)[4] >(Frame::arena(16, 16));
    memcpy(pRows[0], mFlatColor, sizeof(mFlatColor));
    rItem.constants = Frame::addConstants(NEBULA_NONE32, 0, 1, pRows, nullptr, 0);
    rItem.textures = 0;
    rItem.block = pModel->block(getMaterial());
    rItem.slot = pModel->getSlot();
    rItem.generation = pModel->mMotionEpoch;
    rItem.part = pMesh->mIndex;
    rItem.source_hash = getMaterial()->mSourceHash;
    rItem.matrix = pMesh->mFile->single_matrix != NEBULA_MODEL_NONE ? pMesh->mFile->single_matrix : 0;
    rItem.view = Frame::currentView();
    rItem.light_set = Frame::currentLightSet();
    rItem.flags = NEBULA_ITEM_PLAIN_MESH;
    rItem.line_width = 0.0f;
    rItem.point_size = 0.0f;
}
