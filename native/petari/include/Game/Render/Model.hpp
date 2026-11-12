#pragma once

#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class ItemBuffer;
class MaterialEntry;
class Model;
class MeshEntry;

// Turns a joint's place into its matrix. The game's animation is one (XanimeCore); a model file's scale mode is the plain
// one.
class MtxCalc {
public:
    virtual ~MtxCalc() {
    }
    virtual void setWeight(u8, f32) {
    }
    virtual f32 getWeight(u8) const {
        return 0.0f;
    }
    virtual void init(const Vec& rScale, const Mtx& rMtx) = 0;
    virtual void calc() = 0;
};

// The joint walk, and what a matrix calculator reads and writes while it runs: the matrix so far, the scale so far and the
// parent's own scale. Same maths in the three scale modes the files use.
namespace Skeleton {
    extern Mtx mCurrentMtx;
    extern Vec mCurrentS;
    extern Vec mParentS;
    extern Joint* mJoint;
    extern Model* mModel;
    extern MtxCalc* mCurrentMtxCalc;

    void evaluate(Model* pModel, const Vec& rScale, const Mtx& rMtx);

    void initBasic(const Vec& rScale, const Mtx& rMtx);
    void initSoftimage(const Vec& rScale, const Mtx& rMtx);
    void initMaya(const Vec& rScale, const Mtx& rMtx);
    void calcBasic(const TransformInfo& rInfo);
    void calcSoftimage(const TransformInfo& rInfo);
    void calcMaya(const TransformInfo& rInfo);

    void translateRotateMtx(const TransformInfo& rInfo, Mtx mtx);
    void translateRotateMtx(f32 rx, f32 ry, f32 rz, f32 tx, f32 ty, f32 tz, Mtx mtx);

    inline Joint* getJoint() {
        return mJoint;
    }
}  // namespace Skeleton

template < void (*Calc)(const TransformInfo&), void (*Init)(const Vec&, const Mtx&) >
class PlainMtxCalc : public MtxCalc {
public:
    virtual void init(const Vec& rScale, const Mtx& rMtx) {
        Init(rScale, rMtx);
    }
    virtual void calc() {
        Calc(Skeleton::getJoint()->getTransformInfo());
    }
};

// A mesh a model has entered for drawing: chained behind others of its material.
class MeshEntry {
public:
    void drawClear() {
        mNext = nullptr;
    }
    enum {
        Flag_Hidden = 0x10,
        Flag_PluginHidden = 0x20,  // a plugin hid its joint (show_joint)
        Flag_ModHidden = 0x40,     // a mod's override hid it (draw.inst set_visible)
    };
    bool checkFlag(u32 flag) const {
        return (mFlags & flag) != 0;
    }
    bool isHidden() const {
        return checkFlag(Flag_Hidden | Flag_PluginHidden | Flag_ModHidden);
    }
    void onFlag(u32 flag) {
        mFlags |= flag;
    }
    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    Mesh* getMesh() const {
        return mMesh;
    }
    Model* getModel() const {
        return mModel;
    }
    void setMesh(Mesh* pMesh) {
        mMesh = pMesh;
    }
    void setModel(Model* pModel) {
        mModel = pModel;
    }

    MeshEntry* mNext;
    Mesh* mMesh;
    Model* mModel;
    Material* mMaterial;  // what it draws: the model file's material, or the model's own copy
    u32 mFlags;
};

// A material of a model entered in a buffer: its meshes draw together.
class MaterialEntry {
public:
    MaterialEntry();
    void addMeshEntry(MeshEntry* pEntry);
    bool isSame(const MaterialEntry* pOther) const;
    bool isAlone() const {
        return (mMaterialID & cAlone) != 0;
    }
    Material* getMaterial() const {
        return mMaterial;
    }
    MeshEntry* getFirstMeshEntry() const {
        return mMeshEntry;
    }
    int entry(ItemBuffer* pBuffer);
    void draw();
    void drawClear() {
        mNext = nullptr;
        mFirstChild = nullptr;
    }
    enum : u32 {
        cAlone = 1u << 31,
    };

    MaterialEntry* mNext;
    MaterialEntry* mFirstChild;
    MeshEntry* mInitMeshEntry;
    MeshEntry* mMeshEntry;
    Material* mMaterial;
    u32 mMaterialID;
};

// A frame's materials as a flat list, as long as it needs to be, in the order the sort mode asks for. Drawing one appends
// its items to the frame.
class ItemBuffer {
public:
    enum DrawMode {
        DrawMode_Head,
    };
    enum SortMode {
        SortMode_Mat,
        SortMode_MatAnm,
        SortMode_Z,
        SortMode_Non,
    };

    ItemBuffer();
    ~ItemBuffer();
    void frameInit();
    int entryMatSort(MaterialEntry* pEntry);
    int entryZSort(MaterialEntry* pEntry);
    int entryNonSort(MaterialEntry* pEntry);
    void draw();
    void setNonSort() {
        mSortMode = SortMode_Non;
    }
    void setZMtx(MtxPtr pMtx) {
        mZMtx = pMtx;
    }

    u32 mDrawMode;
    u32 mSortMode;

private:
    struct Entry {
        u64 mKey;
        MaterialEntry* mEntry;
    };
    void add(u64 key, MaterialEntry* pEntry);
    void sort();

    MtxPtr mZMtx;
    Entry* mEntries;
    Entry* mScratch;
    u32 mEntryCount;
    u32 mEntryRoom;
};

// The buffers joint entries go into now, and the view drawing happens under.
namespace ModelSys {
    extern Mtx mViewMtx;
    extern ItemBuffer* mDrawBuffer[2];
    extern u32 mViewRole;  // NEBULA_CAMERA_*
    inline MtxPtr getViewMtx() {
        return mViewMtx;
    }
    extern u32 mEntryNum;
}  // namespace ModelSys

enum ModelFlag {
    ModelFlag_None = 0x0,
    ModelFlag_NoView = 0x1,          // draw matrices are its joints' as they are
    ModelFlag_UseDefaultBase = 0x2,  // joints from no base: the view is made with the base at draw
};

typedef void (*ModelCalcCallBack)(Model*, u32);

// A model's matrices as they stood at a view calculation: what its draws read until the next one. A frame's list gets them
// once, when it first draws them.
struct ModelPose {
    Mtx* mJoints;
    u32* mFlags;  // a bit per joint: no scale, its normals turn with its place
    u32 mJointCount;
    u32 mJointRoom;
    const Model* mWriter;    // whose draw matrices it stands for
    ModelPose* mNormalFrom;  // the pose whose normals its draws read, or none: its own
    Mtx mView;
    Mtx mBase;
    Vec mBaseScale;
    u32 mRole;
    u32 mCalcMode;
    u32 mStamp;
    u32 mBlockFrame;
    u32 mBlockStamp;
    u32 mBlock;
    u32 mScaledBlock;
    const NebulaModelMaterial* mScaledFor;
    const f32 (*mListJoints)[4];
    const u32* mListFlags;
};

// One instance of a model file.
class Model {
public:
    Model();
    Model(ModelData* pData, u32 flags, u32 viewCount);
    virtual ~Model() {
    }

    void init(ModelData* pData, u32 flags);

    virtual void update();
    virtual void entry();
    virtual void calc();
    virtual void calcMaterial();
    virtual void viewCalc();

    void calcAnmMtx();

    ModelData* getModelData() const {
        return mModelData;
    }
    void onFlag(u32 flag) {
        mFlags |= flag;
    }
    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    bool checkFlag(u32 flag) const {
        return (mFlags & flag) != 0;
    }
    u32 getMtxCalcMode() const {
        return mFlags & (ModelFlag_NoView | ModelFlag_UseDefaultBase);
    }
    Mtx& getBaseTRMtx() {
        return mBaseTransformMtx;
    }
    void setBaseTRMtx(const Mtx m) {
        NebulaMtxCopy(m, mBaseTransformMtx);
    }
    void setBaseScale(const Vec& rScale) {
        mBaseScale = rScale;
    }
    Vec* getBaseScale() {
        return &mBaseScale;
    }

    MaterialEntry* getMaterialEntry(u32 index) const {
        return &mMaterialEntries[index];
    }
    MeshEntry* getMeshEntry(u32 index) const {
        return &mMeshEntries[index];
    }
    Material* getMaterial(u32 index) const {
        return mMaterials[index];
    }
    Material* ownMaterial(u32 index);

    MtxPtr getAnmMtx(int joint) {
        return mAnmMtx[joint];
    }
    const Mtx* getAnmMtxBuffer() const {
        return mAnmMtx;
    }
    void setAnmMtx(int joint, const Mtx m) {
        NebulaMtxCopy(m, mAnmMtx[joint]);
    }
    void setScaleFlag(int joint, u8 flag) {
        mScaleFlags[joint] = flag;
    }

    // The view its draw matrices are made in.
    const f32 (*viewAtCalc() const)[4] {
        return mViewAtCalc;
    }

    // Its matrices in this frame's list, for a draw of this material; the first draw after a viewCalc puts them there.
    u32 block(const Material* pMaterial);
    void setDrawPose(ModelPose* pPose) {
        mDrawPose = pPose;
    }
    ModelPose* getDrawPose() const {
        return mDrawPose;
    }
    // `pOut` a copy of `rPose` that lasts.
    static void initPose(ModelPose* pPose);
    static void copyPose(ModelPose* pOut, const ModelPose& rPose);
    // Moves a kept pose: its camera becomes `rLeft` x its camera.
    static void movePoseView(ModelPose* pPose, const f32 (*pLeft)[4]);
    u32 getSlot() const {
        return mSlot;
    }

    ModelData* mModelData;
    u32 mFlags;
    ModelCalcCallBack mCalcCallBack;
    uintptr_t mUserArea;
    Vec mBaseScale;
    Mtx mBaseTransformMtx;
    Mtx mViewAtCalc;
    u8* mScaleFlags;
    Mtx* mAnmMtx;
    MaterialEntry* mMaterialEntries;
    MeshEntry* mMeshEntries;
    Material** mMaterials;
    u32 mSlot;
    u32 mMotionEpoch;  // counts the model's lives (MotionSignal)
    u32 mItemJumps;    // counts steps whose constants must not blend into the next (items only; the pose keeps its pair)
    ModelPose mPose;
    ModelPose* mDrawPose;
    ModelPose* mNormalPose;  // the pose the normals were last worked out for

protected:
    void capturePose(ModelPose* pPose, const Mtx* pJoints, u32 jointCount, const u8* pScaleFlags, const f32 (*pBase)[4]);
    u32 poseBlock(ModelPose* pPose);
    u32 addPoseBlock(ModelPose* pPose, const f32* pNormalScale);
};
