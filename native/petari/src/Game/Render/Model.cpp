#include "Game/Render/Model.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/Transform.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_cooked_texture.h>
#include <nebula_geometry.hpp>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace Skeleton {
    Mtx mCurrentMtx;
    Vec mCurrentS;
    Vec mParentS;
    Joint* mJoint;
    Model* mModel;
    MtxCalc* mCurrentMtxCalc;

    void initBasic(const Vec& rScale, const Mtx& rMtx) {
        Vec unitScale = {1.0f, 1.0f, 1.0f};
        mCurrentS = rScale;
        mParentS = unitScale;
        NebulaMtxApplyScale(rMtx, mCurrentMtx, rScale.x, rScale.y, rScale.z);
    }

    void initSoftimage(const Vec& rScale, const Mtx& rMtx) {
        mCurrentS = rScale;
        NebulaMtxCopy(rMtx, mCurrentMtx);
    }

    void initMaya(const Vec& rScale, const Mtx& rMtx) {
        mParentS = (Vec){1.0f, 1.0f, 1.0f};
        mCurrentS = rScale;
        NebulaMtxApplyScale(rMtx, mCurrentMtx, rScale.x, rScale.y, rScale.z);
    }

    namespace {
        inline bool isScaleOne(const Vec& rScale) {
            return rScale.x == 1.0f && rScale.y == 1.0f && rScale.z == 1.0f;
        }
    }  // namespace

    void calcBasic(const TransformInfo& rInfo) {
        const u32 joint = mJoint->getJntNo();
        MtxPtr anmMtx = mModel->getAnmMtx(joint);

        mCurrentS.x *= rInfo.mScale.x;
        mCurrentS.y *= rInfo.mScale.y;
        mCurrentS.z *= rInfo.mScale.z;
        translateRotateMtx(rInfo, anmMtx);

        if (!isScaleOne(mCurrentS)) {
            mModel->setScaleFlag(joint, 0);
            NebulaMtxApplyScale(anmMtx, anmMtx, rInfo.mScale.x, rInfo.mScale.y, rInfo.mScale.z);
        } else {
            mModel->setScaleFlag(joint, 1);
        }

        NebulaMtxConcat(mCurrentMtx, anmMtx, mCurrentMtx);
        NebulaMtxCopy(mCurrentMtx, anmMtx);
    }

    void calcSoftimage(const TransformInfo& rInfo) {
        const u32 joint = mJoint->getJntNo();
        MtxPtr anmMtx = mModel->getAnmMtx(joint);

        translateRotateMtx(rInfo.mRotation.x, rInfo.mRotation.y, rInfo.mRotation.z, rInfo.mTranslate.x * mCurrentS.x,
                           rInfo.mTranslate.y * mCurrentS.y, rInfo.mTranslate.z * mCurrentS.z, anmMtx);
        NebulaMtxConcat(mCurrentMtx, anmMtx, mCurrentMtx);

        mCurrentS.x *= rInfo.mScale.x;
        mCurrentS.y *= rInfo.mScale.y;
        mCurrentS.z *= rInfo.mScale.z;

        if (!isScaleOne(mCurrentS)) {
            mModel->setScaleFlag(joint, 0);
            NebulaMtxApplyScale(mCurrentMtx, anmMtx, mCurrentS.x, mCurrentS.y, mCurrentS.z);
            anmMtx[0][3] = mCurrentMtx[0][3];
            anmMtx[1][3] = mCurrentMtx[1][3];
            anmMtx[2][3] = mCurrentMtx[2][3];
        } else {
            mModel->setScaleFlag(joint, 1);
            NebulaMtxCopy(mCurrentMtx, anmMtx);
        }
    }

    void calcMaya(const TransformInfo& rInfo) {
        Joint* pJoint = mJoint;
        const u32 joint = pJoint->getJntNo();
        MtxPtr anmMtx = mModel->getAnmMtx(joint);

        translateRotateMtx(rInfo, anmMtx);

        if (isScaleOne(rInfo.mScale)) {
            mModel->setScaleFlag(joint, 1);
        } else {
            mModel->setScaleFlag(joint, 0);
            NebulaMtxApplyScale(anmMtx, anmMtx, rInfo.mScale.x, rInfo.mScale.y, rInfo.mScale.z);
        }

        if (pJoint->getScaleCompensate() == 1) {
            Vec inverseScale;
            inverseScale.x = fastReciprocal(mParentS.x);
            inverseScale.y = fastReciprocal(mParentS.y);
            inverseScale.z = fastReciprocal(mParentS.z);

            anmMtx[0][0] *= inverseScale.x;
            anmMtx[0][1] *= inverseScale.x;
            anmMtx[0][2] *= inverseScale.x;
            anmMtx[1][0] *= inverseScale.y;
            anmMtx[1][1] *= inverseScale.y;
            anmMtx[1][2] *= inverseScale.y;
            anmMtx[2][0] *= inverseScale.z;
            anmMtx[2][1] *= inverseScale.z;
            anmMtx[2][2] *= inverseScale.z;
        }

        NebulaMtxConcat(mCurrentMtx, anmMtx, mCurrentMtx);
        NebulaMtxCopy(mCurrentMtx, anmMtx);

        mParentS.x = rInfo.mScale.x;
        mParentS.y = rInfo.mScale.y;
        mParentS.z = rInfo.mScale.z;
    }

    namespace {
        struct Saved {
            Mtx mMtx;
            Vec mS;
            Vec mParentS;
            MtxCalc* mPreviousCalc;
            JointCallBack mCallBack;
        };
        Saved* sStack;
        u32 sStackRoom;
    }  // namespace

    void evaluate(Model* pModel, const Vec& rScale, const Mtx& rMtx) {
        ModelData* pData = pModel->getModelData();
        if (pData->getJointCount() == 0) {
            return;
        }
        if (sStackRoom < pData->getJointCount()) {
            const NebulaHostServices* pHost = nebula_host();
            sStackRoom = pData->getJointCount() * 2u;
            sStack = static_cast< Saved* >(pHost->heap_realloc(pHost->user, sStack, sStackRoom * sizeof(Saved)));
        }
        mModel = pModel;
        MtxCalc* pBasic = pData->getBasicMtxCalc();
        pBasic->init(rScale, rMtx);
        mCurrentMtxCalc = pBasic;

        const NebulaModelStep* pSteps = reinterpret_cast< const NebulaModelStep* >(pData->at(pData->mFile->steps_at));
        const u32 count = pData->mFile->step_count;
        Saved* pTop = sStack;
        for (u32 i = 0; i < count; i++) {
            Joint* pJoint = pData->getJointAt(pSteps[i].joint);
            if (pSteps[i].op == NEBULA_STEP_ENTER) {
                NebulaMtxCopy(mCurrentMtx, pTop->mMtx);
                pTop->mS = mCurrentS;
                pTop->mParentS = mParentS;
                pTop->mPreviousCalc = nullptr;
                MtxCalc* pCalc = pJoint->getMtxCalc();
                if (pCalc != nullptr) {
                    pTop->mPreviousCalc = mCurrentMtxCalc;
                    mJoint = pJoint;
                    mCurrentMtxCalc = pCalc;
                    pCalc->calc();
                } else if (mCurrentMtxCalc != nullptr) {
                    mJoint = pJoint;
                    mCurrentMtxCalc->calc();
                }
                JointCallBack callBack = pJoint->getCallBack();
                pTop->mCallBack = callBack;
                if (callBack != nullptr) {
                    callBack(pJoint, 0);
                }
                pTop++;
            } else {
                pTop--;
                NebulaMtxCopy(pTop->mMtx, mCurrentMtx);
                mCurrentS = pTop->mS;
                mParentS = pTop->mParentS;
                if (pTop->mPreviousCalc != nullptr) {
                    mCurrentMtxCalc = pTop->mPreviousCalc;
                }
                if (pTop->mCallBack != nullptr) {
                    pTop->mCallBack(pJoint, 1);
                }
            }
        }
    }
}  // namespace Skeleton

namespace ModelSys {
    Mtx mViewMtx;
    ItemBuffer* mDrawBuffer[2];
    u32 mViewRole;
    u32 mEntryNum;

    u32 currentCamera() {
        return Frame::camera(mViewMtx, mViewRole);
    }
}  // namespace ModelSys

MaterialEntry::MaterialEntry() {
    mNext = nullptr;
    mFirstChild = nullptr;
    mInitMeshEntry = nullptr;
    mMeshEntry = nullptr;
    mMaterial = nullptr;
    mMaterialID = NEBULA_NONE32;
}

void MaterialEntry::addMeshEntry(MeshEntry* pEntry) {
    if (mMeshEntry == nullptr) {
        mMeshEntry = pEntry;
    } else {
        pEntry->mNext = mMeshEntry;
        mMeshEntry = pEntry;
    }
}

bool MaterialEntry::isSame(const MaterialEntry* pOther) const {
    return mMaterialID == pOther->mMaterialID && (mMaterialID >> 31) == 0;
}

int MaterialEntry::entry(ItemBuffer* pBuffer) {
    switch (pBuffer->mSortMode) {
    case ItemBuffer::SortMode_Z:
        return pBuffer->entryZSort(this);
    case ItemBuffer::SortMode_Non:
        return pBuffer->entryNonSort(this);
    case ItemBuffer::SortMode_Mat:
    case ItemBuffer::SortMode_MatAnm:
    default:
        return pBuffer->entryMatSort(this);
    }
}

void MaterialEntry::draw() {
    MaterialDraw draw;
    draw.send(mMaterial);
    for (MeshEntry* pShape = mMeshEntry; pShape != nullptr; pShape = pShape->mNext) {
        draw.sendOwn(pShape->mMaterial, mMaterial);
        if (!pShape->isHidden() && pShape->mMesh != nullptr) {
            draw.draw(pShape->mModel, pShape->mMesh);
        }
    }
}

ItemBuffer::ItemBuffer() {
    mDrawMode = DrawMode_Head;
    mSortMode = SortMode_Mat;
    mZMtx = nullptr;
    mEntries = nullptr;
    mScratch = nullptr;
    mEntryCount = 0;
    mEntryRoom = 0;
}

ItemBuffer::~ItemBuffer() {
    delete[] mEntries;
    delete[] mScratch;
}

void ItemBuffer::frameInit() {
    mEntryCount = 0;
}

void ItemBuffer::add(u64 key, MaterialEntry* pEntry) {
    if (mEntryCount == mEntryRoom) {
        const u32 room = mEntryRoom != 0 ? mEntryRoom * 2 : 64;
        Entry* pEntries = new Entry[room];
        if (mEntryCount != 0) {
            memcpy(pEntries, mEntries, mEntryCount * sizeof(Entry));
        }
        delete[] mEntries;
        delete[] mScratch;
        mEntries = pEntries;
        mScratch = new Entry[room];
        mEntryRoom = room;
    }
    mEntries[mEntryCount].mKey = key;
    mEntries[mEntryCount].mEntry = pEntry;
    mEntryCount++;
}

int ItemBuffer::entryMatSort(MaterialEntry* pEntry) {
    pEntry->drawClear();
    pEntry->getFirstMeshEntry()->drawClear();

    // The same image together, wherever it was loaded, so that draw order does not depend on memory layout.
    u32 image = 0;
    if (!pEntry->isAlone()) {
        Material* pMaterial = pEntry->getMaterial();
        const u32 texNo = pMaterial->getTextureIndex(0);
        if (texNo != NEBULA_NONE32) {
            const ResTIMG* pTimg = pMaterial->getModelData()->getTexture(texNo);
            const NebulaTexture* pRecord = nebula_texture_record(pTimg->image());
            image = pRecord != nullptr ? static_cast< u32 >(pRecord->image_id) : 0;
        }
        for (u32 i = 0; i < mEntryCount; i++) {
            if (static_cast< u32 >(mEntries[i].mKey >> 32) == image && mEntries[i].mEntry->isSame(pEntry)) {
                mEntries[i].mEntry->addMeshEntry(pEntry->getFirstMeshEntry());
                return 0;
            }
        }
    }
    add(static_cast< u64 >(image) << 32, pEntry);
    return 1;
}

int ItemBuffer::entryZSort(MaterialEntry* pEntry) {
    pEntry->drawClear();
    pEntry->getFirstMeshEntry()->drawClear();

    const f32 x = mZMtx[0][3];
    const f32 y = mZMtx[1][3];
    const f32 z = mZMtx[2][3];

    // Far first: the key is the distance's bits turned round, in front of the place in the list.
    const f32 (*pView)[4] = ModelSys::mViewMtx;
    const f32 distance = -((z * pView[2][2] + x * pView[2][0]) + (pView[2][3] + y * pView[2][1]));
    const f32 far = distance > 0.0f ? distance : 0.0f;
    add(static_cast< u64 >(0xFFFFFFFFu - __builtin_bit_cast(u32, far)) << 32, pEntry);
    return 1;
}

int ItemBuffer::entryNonSort(MaterialEntry* pEntry) {
    pEntry->drawClear();
    pEntry->getFirstMeshEntry()->drawClear();

    add(0, pEntry);
    return 1;
}

// By key; of equal keys, the one entered last first.
void ItemBuffer::sort() {
    for (u32 i = 0; i < mEntryCount; i++) {
        mEntries[i].mKey |= mEntryCount - 1 - i;
    }
    Entry* pFrom = mEntries;
    Entry* pTo = mScratch;
    for (u32 width = 1; width < mEntryCount; width *= 2) {
        for (u32 start = 0; start < mEntryCount; start += width * 2) {
            const u32 middle = start + width < mEntryCount ? start + width : mEntryCount;
            const u32 end = start + width * 2 < mEntryCount ? start + width * 2 : mEntryCount;
            u32 left = start;
            u32 right = middle;
            for (u32 out = start; out < end; out++) {
                const bool takeLeft = left < middle && (right >= end || pFrom[left].mKey <= pFrom[right].mKey);
                pTo[out] = takeLeft ? pFrom[left++] : pFrom[right++];
            }
        }
        Entry* pSwap = pFrom;
        pFrom = pTo;
        pTo = pSwap;
    }
    mEntries = pFrom;
    mScratch = pTo;
}

void ItemBuffer::draw() {
    sort();
    if (mDrawMode == DrawMode_Head) {
        for (u32 i = 0; i < mEntryCount; i++) {
            mEntries[i].mEntry->draw();
        }
    } else {
        for (u32 i = mEntryCount; i != 0; i--) {
            mEntries[i - 1].mEntry->draw();
        }
    }
    // Drawn again, its entries are in order already: their keys come from the same place.
    for (u32 i = 0; i < mEntryCount; i++) {
        mEntries[i].mKey &= 0xFFFFFFFF00000000ull;
    }
}

const Mtx cIdentityMtx = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
};

namespace {
    const Vec cOneScale = {1.0f, 1.0f, 1.0f};
    const Mtx cIdentity = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
}  // namespace

Model::Model() {
    memset(static_cast< void* >(&mModelData), 0, reinterpret_cast< u8* >(&mNormalPose + 1) - reinterpret_cast< u8* >(&mModelData));
    mBaseScale.x = mBaseScale.y = mBaseScale.z = 1.0f;
    NebulaMtxIdentity(mBaseTransformMtx);
}

Model::Model(ModelData* pData, u32 flags, u32) : Model() {
    init(pData, flags);
}

void Model::init(ModelData* pData, u32 flags) {
    mModelData = pData;
    mSlot = Frame::newSlot();
    mFlags = flags & (ModelFlag_NoView | ModelFlag_UseDefaultBase);

    const u32 jointNum = pData->getJointCount();
    if (jointNum != 0) {
        mScaleFlags = new u8[jointNum];
        mAnmMtx = new Mtx[jointNum];
    }
    initPose(&mPose);
    mDrawPose = &mPose;

    const u32 shapeNum = pData->getMeshCount();
    if (shapeNum != 0) {
        mMeshEntries = new MeshEntry[shapeNum];
        memset(mMeshEntries, 0, shapeNum * sizeof(MeshEntry));
        for (u32 i = 0; i < shapeNum; i++) {
            mMeshEntries[i].setMesh(pData->getMeshAt(i));
            mMeshEntries[i].setModel(this);
        }
    }
    const u32 materialNum = pData->getMaterialCount();
    if (materialNum != 0) {
        mMaterialEntries = new MaterialEntry[materialNum];
        mMaterials = new Material*[materialNum];
    }
    for (u32 i = 0; i < materialNum; i++) {
        Material* pMaterial = pData->getMaterialAt(i);
        MaterialEntry* pEntry = &mMaterialEntries[i];
        MeshEntry* pShape = &mMeshEntries[pMaterial->getMesh()->getIndex()];
        mMaterials[i] = pMaterial;
        pShape->mMaterial = pMaterial;
        pEntry->mMaterial = pMaterial;
        pEntry->mInitMeshEntry = pShape;
        pEntry->addMeshEntry(pShape);
        pEntry->mMaterialID = pMaterial->getID();
    }
}

void Model::initPose(ModelPose* pPose) {
    memset(pPose, 0, sizeof(*pPose));
    pPose->mBlock = NEBULA_NONE32;
    pPose->mScaledBlock = NEBULA_NONE32;
}

Material* Model::ownMaterial(u32 index) {
    Material* pShared = mModelData->getMaterialAt(index);
    if (mMaterials[index] == pShared) {
        Material* pOwn = new Material(*pShared);
        pOwn->mForms = nullptr;
        mMaterials[index] = pOwn;
        mMeshEntries[pShared->getMesh()->getIndex()].mMaterial = pOwn;
    }
    return mMaterials[index];
}

void Model::calcMaterial() {
    const u32 materialNum = mModelData->getMaterialCount();
    for (u32 i = 0; i < materialNum; i++) {
        Material* pMaterial = mMaterials[i];
        if (pMaterial != mModelData->getMaterialAt(i)) {
            pMaterial->calc();
        }
    }
}

void Model::calcAnmMtx() {
    if (checkFlag(ModelFlag_UseDefaultBase)) {
        Skeleton::evaluate(this, cOneScale, cIdentity);
    } else {
        Skeleton::evaluate(this, mBaseScale, mBaseTransformMtx);
    }
}

void Model::update() {
    calc();
    entry();
}

void Model::calc() {
    calcAnmMtx();
    if (mCalcCallBack != nullptr) {
        mCalcCallBack(this, 0);
    }
}

void Model::entry() {
    for (u32 i = 0; i < mModelData->getJointCount(); i++) {
        Joint* pJoint = mModelData->getJointAt(i);
        if (pJoint->getMesh() == nullptr) {
            continue;
        }
        MtxPtr anmMtx = mAnmMtx[i];
        ModelSys::mDrawBuffer[0]->setZMtx(anmMtx);
        ModelSys::mDrawBuffer[1]->setZMtx(anmMtx);
        for (Material* pMaterial = pJoint->getMesh(); pMaterial != nullptr; pMaterial = pMaterial->getNext()) {
            if (pMaterial->getMesh()->isHidden()) {
                continue;
            }
            MaterialEntry* pEntry = &mMaterialEntries[pMaterial->getIndex()];
            pEntry->mMeshEntry = &mMeshEntries[pMaterial->getMesh()->getIndex()];
            ItemBuffer* pBuffer = ModelSys::mDrawBuffer[pMaterial->isTranslucent() ? 1 : 0];
            if (pEntry->entry(pBuffer) != 0) {
                ModelSys::mEntryNum++;
            }
        }
    }
}

namespace {
    void growPose(ModelPose* pPose, u32 jointCount) {
        if (pPose->mJointRoom < jointCount) {
            delete[] pPose->mJoints;
            delete[] pPose->mFlags;
            pPose->mJoints = new Mtx[jointCount];
            pPose->mFlags = new u32[jointCount / 32u + 1u];
            pPose->mJointRoom = jointCount;
        }
        pPose->mJointCount = jointCount;
    }
}  // namespace

void Model::capturePose(ModelPose* pPose, const Mtx* pJoints, u32 jointCount, const u8* pScaleFlags, const f32 (*pBase)[4]) {
    growPose(pPose, jointCount);
    if (jointCount != 0) {
        memcpy(pPose->mJoints, pJoints, jointCount * sizeof(Mtx));
        memset(pPose->mFlags, 0, (jointCount / 32u + 1u) * sizeof(u32));
        if (pScaleFlags != nullptr) {
            for (u32 i = 0; i < jointCount; i++) {
                pPose->mFlags[i / 32u] |= static_cast< u32 >(pScaleFlags[i] != 0) << (i % 32u);
            }
        }
    }
    NebulaMtxCopy(ModelSys::mViewMtx, pPose->mView);
    pPose->mRole = ModelSys::mViewRole;
    pPose->mCalcMode = getMtxCalcMode();
    pPose->mBaseScale = mBaseScale;
    NebulaMtxCopy(pBase, pPose->mBase);
    pPose->mWriter = this;
    pPose->mNormalFrom = nullptr;
    pPose->mStamp++;
}

void Model::copyPose(ModelPose* pOut, const ModelPose& rPose) {
    growPose(pOut, rPose.mJointCount);
    if (rPose.mJointCount != 0) {
        memcpy(pOut->mJoints, rPose.mJoints, rPose.mJointCount * sizeof(Mtx));
        memcpy(pOut->mFlags, rPose.mFlags, (rPose.mJointCount / 32u + 1u) * sizeof(u32));
    }
    NebulaMtxCopy(rPose.mView, pOut->mView);
    NebulaMtxCopy(rPose.mBase, pOut->mBase);
    pOut->mBaseScale = rPose.mBaseScale;
    pOut->mRole = NEBULA_CAMERA_OTHER;
    pOut->mCalcMode = rPose.mCalcMode;
    pOut->mWriter = rPose.mWriter;
    pOut->mNormalFrom = rPose.mNormalFrom;
    pOut->mStamp++;
}

void Model::movePoseView(ModelPose* pPose, const f32 (*pLeft)[4]) {
    NebulaMtxConcat(pLeft, pPose->mView, pPose->mView);
    pPose->mStamp++;
}

void Model::viewCalc() {
    capturePose(&mPose, mAnmMtx, mModelData->getJointCount(), mScaleFlags, mBaseTransformMtx);
    NebulaMtxCopy(ModelSys::mViewMtx, mViewAtCalc);
    mDrawPose = &mPose;
    mNormalPose = &mPose;
}

u32 Model::poseBlock(ModelPose* pPose) {
    if (pPose->mBlockFrame != Frame::number() || pPose->mBlockStamp != pPose->mStamp || pPose->mBlock == NEBULA_NONE32) {
        pPose->mListJoints = nullptr;
        pPose->mListFlags = nullptr;
        pPose->mBlock = addPoseBlock(pPose, nullptr);
        pPose->mScaledBlock = NEBULA_NONE32;
        pPose->mBlockFrame = Frame::number();
        pPose->mBlockStamp = pPose->mStamp;
    }
    return pPose->mBlock;
}

u32 Model::block(const Material* pMaterial) {
    ModelPose* pPose = mDrawPose;
    const u32 plain = poseBlock(pPose);
    if (pMaterial->mFile->has_scale == 0) {
        return plain;
    }
    // A bump map's normals scale by its material.
    if (pPose->mScaledBlock == NEBULA_NONE32 || pPose->mScaledFor != pMaterial->mFile) {
        pPose->mScaledBlock = addPoseBlock(pPose, pMaterial->mFile->normal_scale);
        pPose->mScaledFor = pMaterial->mFile;
    }
    return pPose->mScaledBlock;
}

u32 Model::addPoseBlock(ModelPose* pPose, const f32* pNormalScale) {
    const u32 jointNum = pPose->mJointCount;
    const u32 flagWords = jointNum / 32u + 1u;
    NebulaFrameBlock block;
    memset(&block, 0, sizeof(block));
    if (pPose->mListJoints == nullptr) {
        f32 (*pJoints)[4] = static_cast< f32 (*)[4] >(Frame::arena(jointNum * sizeof(Mtx), 16));
        memcpy(pJoints, pPose->mJoints, jointNum * sizeof(Mtx));
        u32* pFlags = Frame::alloc< u32 >(flagWords);
        memcpy(pFlags, pPose->mFlags, flagWords * sizeof(u32));
        pPose->mListJoints = pJoints;
        pPose->mListFlags = pFlags;
    }
    const Model* pWriter = pPose->mWriter;
    block.joints = pPose->mListJoints;
    block.scale_flags = pPose->mListFlags;
    block.model = pWriter->mModelData->mGpuModel;
    block.joint_count = jointNum;
    block.slot = pWriter->mSlot;
    block.generation = pWriter->mMotionEpoch;
    block.flags = NEBULA_BLOCK_WORLD_ROWS;
    block.camera = Frame::camera(pPose->mView, pPose->mRole);
    block.calc_mode = pPose->mCalcMode;
    block.normals_from = pPose->mNormalFrom != nullptr ? poseBlock(pPose->mNormalFrom) : NEBULA_NONE32;
    if (pPose->mCalcMode == 2) {
        // The camera's matrix takes the base with its scale in its columns; the translation stays.
        for (u32 r = 0; r < 3; r++) {
            block.base[r][0] = pPose->mBase[r][0] * pPose->mBaseScale.x;
            block.base[r][1] = pPose->mBase[r][1] * pPose->mBaseScale.y;
            block.base[r][2] = pPose->mBase[r][2] * pPose->mBaseScale.z;
            block.base[r][3] = pPose->mBase[r][3];
        }
    }
    if (pNormalScale != nullptr) {
        block.normal_scale[0] = pNormalScale[0];
        block.normal_scale[1] = pNormalScale[1];
        block.normal_scale[2] = pNormalScale[2];
    }
    return Frame::addBlock(block);
}
