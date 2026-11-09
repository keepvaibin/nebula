#pragma once

#include <NebulaGpu.h>
#include <nebula_frame_list.h>
#include <nebula_math.h>
#include <nebula_model.h>
#include "Game/Render/Clip.hpp"

class Joint;
class Mesh;
class Model;
class ModelData;
class TypedDraw;

// Values fog is drawn with.
struct FogInfo {
    u8 mType;
    f32 mStartZ;
    f32 mEndZ;
    f32 mNearZ;
    f32 mFarZ;
    NebulaColor8 mColor;
};

// A material's texture matrix: its animated part and how it is made.
struct CoordMatrixInfo {
    u8 mProjection;
    u8 mInfo;
    Vec mCenter;
    TextureTransform mSRT;
    Mtx44 mEffectMtx;
};

enum CoordMatrixMode {
    CoordMatrixMode_None,
    CoordMatrixMode_EnvmapBasic,
    CoordMatrixMode_ProjmapBasic,
    CoordMatrixMode_ViewProjmapBasic,
    CoordMatrixMode_ViewProjmapBasic2,  // made as ViewProjmapBasic
    CoordMatrixMode_ProjmapBasic2,      // made as ProjmapBasic
    CoordMatrixMode_EnvmapOld,
    CoordMatrixMode_Envmap,
    CoordMatrixMode_Projmap,
    CoordMatrixMode_ViewProjmap,
    CoordMatrixMode_EnvmapOldEffectMtx,
    CoordMatrixMode_EnvmapEffectMtx,
};

// A matrix place in a material's coordinate: the identity, or matrix n at NEBULA_COORD_MATRIX0 + n * 3.
enum : u32 {
    NEBULA_COORD_MATRIX0 = 0,
    NEBULA_COORD_IDENTITY = NEBULA_NONE32,
};

class CoordMatrix {
public:
    CoordMatrix();
    void calc();
    // mInfo.mInfo: the mode in bits 0-5, the Maya SRT order in bit 7.
    u32 getMode() const {
        return mInfo.mInfo & 0x3F;
    }

    bool isMaya() const {
        return ((mInfo.mInfo >> 7) & 1) != 0;
    }

    // Every other mode with a matrix has eye space as its input.
    bool readsEye() const {
        return getMode() != CoordMatrixMode_None && !followsView();
    }

    bool followsView() const {
        const u32 mode = getMode();
        return mode == CoordMatrixMode_ProjmapBasic || mode == CoordMatrixMode_ProjmapBasic2 || mode == CoordMatrixMode_Projmap ||
               mode == CoordMatrixMode_EnvmapOldEffectMtx || mode == CoordMatrixMode_EnvmapEffectMtx;
    }

    CoordMatrixInfo& getInfo() {
        return mInfo;
    }
    Mtx& getMtx() {
        return mMtx;
    }
    void setEffectMtx(const Mtx mtx);

    CoordMatrixInfo mInfo;

private:
    Mtx mMtx;
};

// A coordinate of a material: what generates it and through which of its matrices.
struct MaterialCoord {
    u8 mType;
    u8 mSource;
    u32 mMatrix;

    u32 getMatrix() const {
        return mMatrix;
    }
    void setMatrix(u32 matrix) {
        mMatrix = matrix;
    }
};

// A material: everything its draw reads, as plain values, starting as the file gave them. A model file's materials are
// shared by its models; a model whose material moves (animation, a controller) draws its own copy (Model::ownMaterial).
class Material {
public:
    void init(ModelData* pData, u32 index);

    // The texture matrices, once the values they are made from changed.
    void calc();

    u32 getIndex() const {
        return mIndex;
    }
    u32 getID() const {
        return mFile->same_as;
    }
    // The model file's draw mode (model_ir.rs read_material; a made material is 4 when it blends, else 1): with neither
    // of the low two bits it draws in the translucent buffer.
    bool isTranslucent() const {
        return (mFile->mode & 3) == 0;
    }
    Material* getNext() const {
        return mNext;
    }
    Mesh* getMesh() const {
        return mMesh;
    }
    Joint* getJoint() const {
        return mJoint;
    }
    ModelData* getModelData() const {
        return mData;
    }

    CoordMatrix* getCoordMatrix(u32 index) {
        return (mMatrixUsed & (1u << index)) != 0 ? &mMatrices[index] : nullptr;
    }
    void setCoordMatrix(u32 index, const CoordMatrix& rMatrix) {
        mMatrices[index] = rMatrix;
        mMatrixUsed |= static_cast< u16 >(1u << index);
        touch();
    }
    MaterialCoord* getCoord(u32 index) {
        touch();
        return &mCoords[index];
    }
    u32 getCoordCount() const {
        return mFile->coord_count;
    }
    u32 getTextureIndex(u32 map) const {
        return mTextureIndex[map];
    }
    void setTextureIndex(u32 map, u32 number) {
        mTextureIndex[map] = number;
        mTextureStamp++;
    }
    NebulaColor8* getMaterialColor(u32 index) {
        touch();
        return &mMaterialColor[index];
    }
    void setMaterialColor(u32 index, const NebulaColor8* pColor) {
        mMaterialColor[index] = *pColor;
        touch();
    }
    NebulaColor8* getAmbientColor(u32 index) {
        touch();
        return &mAmbientColor[index];
    }
    NebulaColor16* getRegisterColor(u32 index) {
        touch();
        return &mRegisters[index];
    }
    NebulaColor8* getConstantColor(u32 index) {
        touch();
        return &mConstants[index];
    }
    FogInfo* getFog() {
        touch();
        return &mFog;
    }
    bool hasAmbient() const {
        return (mFile->flags & NEBULA_MATERIAL_HAS_AMBIENT) != 0;
    }
    u8 getAlphaRef(u32 index) const {
        return index == 0 ? mFile->alpha_ref0 : mFile->alpha_ref1;
    }

    bool usesMap(u32 map) const {
        return (mFile->used_maps & (1u << map)) != 0;
    }
    bool usesCoord(u32 coord) const {
        return (mFile->used_coords & (1u << coord)) != 0;
    }
    bool usesLight(u32 channel, u32 light) const {
        const NebulaModelChannel& rChannel = mFile->channels[channel];
        return rChannel.enable != 0 && (rChannel.light_mask & (1u << light)) != 0;
    }

    void touch() {
        mStamp++;
    }

    // The form wanted, or -1 when the model was not cooked with it.
    struct FormKey {
        u8 mReplace;
        u8 mPass;
        bool mNormalCoord;
        bool mFogLinear;
        bool mLit;
    };
    s32 findForm(const FormKey& rKey) const;
    const NebulaModelForm* getForm(s32 index) const;

    // A form's rows as this material fills them: NEBULA_ROW_* in the form's order, vertex rows then pixel rows.
    void fillRows(const NebulaModelForm& rForm, f32 (*pOut)[4]) const;
    // One row by its number.
    void fillRow(u32 row, f32* pOut) const;

    const NebulaModelMaterial* mFile;
    ModelData* mData;
    Material* mNext;
    Mesh* mMesh;
    Joint* mJoint;
    u32 mIndex;
    u16 mMatrixUsed;
    u32 mTextureIndex[NEBULA_MATERIAL_SLOTS];
    NebulaColor8 mMaterialColor[2];
    NebulaColor8 mAmbientColor[2];
    NebulaColor16 mRegisters[4];
    NebulaColor8 mConstants[4];
    FogInfo mFog;
    MaterialCoord mCoords[NEBULA_MATERIAL_SLOTS];
    CoordMatrix mMatrices[NEBULA_MATERIAL_SLOTS];
    u32 mStamp;
    u32 mTextureStamp;

    // What it has handed the renderer, form by form.
    struct FormState {
        u32 mSlot;
        u32 mSlotStamp;
        u32 mTable;
        u32 mTableStamp;
        u32 mTableDataStamp;
        u32 mPipeline;
        u32 mPipelineState;
        u32 mPipelineAttributes, mPipelineStride;
        u32 mConstantsFrame;
        u32 mConstantsStamp;
        u32 mConstantsIndex;
        u32* mCoordRows;
        u32 mCoordRowCount;
        bool mAnalysed;
        bool mReadsAmbient;
    };
    FormState* formState(s32 index);

    FormState* mForms;
    u32 mSourceHash;  // the model's name and its own, from frame to frame and for ray tracing
    bool mFollowsView;
};

// The light environment's ambient colours (0-1 floats): a material with its own sets them, an actor's light after that,
// and they stay for the materials after it.
namespace MaterialAmbient {
    void set(u32 channel, NebulaColor8 color);
    const f32* get(u32 channel);
}  // namespace MaterialAmbient

// Where a mesh keeps a vertex input, or the zero stream: appended to `pInputs` at pDesc->input_count, which counts it.
void meshLayoutInput(NebulaPipelineDesc* pDesc, NebulaLayoutInput* pInputs, u16 semantic, const NebulaModelMesh& rMesh);

// A colour register a draw sets, by name: the numbers are the material data's (0 the stage output, which no draw sets), so a
// call can never land one register down.
enum MaterialRegister : u32 {
    MaterialRegister_0 = 1,
    MaterialRegister_1 = 2,
    MaterialRegister_2 = 3,
};

// A material set up for draws: which form it draws, and what these draws change of its values. Draws take everything from
// here; nothing carries to another set-up but the ambient colours.
class MaterialDraw {
public:
    MaterialDraw();
    explicit MaterialDraw(Material* pMaterial);

    // Starts over with `pMaterial`: its ambient colours load.
    void send(Material* pMaterial);
    // A model's own copy of the material sent: the same form, its values, the ambient colours as they stand.
    void sendOwn(Material* pMaterial, const Material* pShared);

    // A pass that replaces its stages (`mode` 1-5; alphas A0, A1; drawn above `ref`, -1 always).
    void override(u32 mode, f32 a0, f32 a1, f32 ref);
    // The alpha written over the reflection pass (override mode 5).
    void overrideAlpha(f32 alpha);
    // Coordinate 0 the vertices' normals and texture 0 `texture` (`width` x `height`), repeated, linear.
    void overrideCoordinate(u32 texture, u32 width, u32 height);
    // Another image in a map (a character's swapped eyes or hands).
    void swapImage(u32 map, u32 texture);
    // A form edited for a pass (NEBULA_FORM_PASS_*), added to those already on.
    void applyPass(u8 pass);

    void setCoordinate(u32 coord, const f32 (*pMtx)[4]);
    void setRegister(MaterialRegister reg, NebulaColor8 color);
    const f32* getRegister(MaterialRegister reg);
    void setMaterialColor(u32 channel, NebulaColor8 color);
    void setAmbient(u32 channel, NebulaColor8 color);
    void setFog(f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    // The draw's pipeline state in place of the material's.
    void setState(u32 state);
    u32 getState() const;
    // Flat colour in place of the material (NEBULA_SHADER_FLAT_COLOR).
    void setFlatColor(f32 r, f32 g, f32 b, f32 a);

    // A draw of `pMesh` under the matrices `pModel` made last.
    void draw(Model* pModel, Mesh* pMesh);
    // One mesh of a model file under `rDraw`'s single matrix, in the material's own form.
    void drawUnder(TypedDraw& rDraw, Mesh* pMesh);
    // Sets a site's draws on this material's form and values, its vertices its own.
    void bind(TypedDraw& rDraw);
    // The same with another mesh in place of the material's own.
    Material* getMaterial() const {
        return mOwn != nullptr ? mOwn : mMaterial;
    }

private:
    enum {
        Edit_Register1 = 1u << 0,    // and 2, 3
        Edit_MaterialColor0 = 1u << 3,
        Edit_MaterialColor1 = 1u << 4,
        Edit_Ambient0 = 1u << 5,
        Edit_Ambient1 = 1u << 6,
        Edit_Fog = 1u << 7,
        Edit_Coord0 = 1u << 8,       // through slot 15
        Edit_Scale0 = 1u << 24,
    };

    void clear();
    s32 resolveForm();
    void applyEdits(const u32* pRows, u32 count, f32 (*pOut)[4]) const;
    void drawFlat(Model* pModel, Mesh* pMesh);
    u32 constantsOf(s32 formIndex, const NebulaModelForm& rForm);
    u32 tableOf(s32 formIndex, const NebulaModelForm& rForm);
    u32 pipelineOf(s32 formIndex, const NebulaModelForm& rForm, const Mesh* pMesh);

    Material* mMaterial;
    Material* mOwn;
    Material::FormKey mKey;
    u32 mEdits;
    bool mHasOverride;
    bool mHasState;
    bool mZeroAlphaRef;
    bool mCoordinateTexture;
    u32 mState;
    u32 mImageMask;
    u32 mImages[NEBULA_MATERIAL_SLOTS];
    bool mFlat;
    f32 mOverride[4];
    f32 mRegisters[3][4];
    f32 mMaterialColors[2][4];
    f32 mAmbient[2][4];
    f32 mFogValues[2][4];
    f32 mCoords[NEBULA_MATERIAL_SLOTS][3][4];
    f32 mScale0[4];
    f32 mFlatColor[4];
    s32 mForm;
    bool mFormLit;
};
