#include "Game/Util/JointUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include <nebula_geometry.hpp>

namespace MR {
    // A joint name the model lacks (a bad name in a shadow or effect table) is the root joint.
    Joint* getJoint(Model* pModel, const char* pName) {
        const s32 idx = pModel->mModelData->findJoint(pName);
        return pModel->mModelData->getJointAt(idx >= 0 ? static_cast< u32 >(idx) : 0);
    }

    Joint* getJoint(const LiveActor* pActor, const char* pName) {
        return MR::getJoint(MR::getModel(pActor), pName);
    }

    Joint* getJoint(const LiveActor* pActor, u32 idx) {
        return MR::getModelData(pActor)->getJointAt(idx);
    }

    MtxPtr getJointMtx(Model* pModel, const char* pName) {
        const s32 idx = pModel->mModelData->findJoint(pName);
        return pModel->getAnmMtx(idx >= 0 ? idx : 0);
    }

    MtxPtr getJointMtx(const LiveActor* pActor, const char* pName) {
        const s32 idx = MR::getModelData(pActor)->findJoint(pName);
        return getJointMtx(pActor, idx >= 0 ? idx : 0);
    }

    MtxPtr getJointMtx(const LiveActor* pActor, int idx) {
        return MR::getModel(pActor)->getAnmMtx(idx);
    }

    u32 getJointIndex(const LiveActor* pActor, const char* pName) {
        // A joint name the model lacks is the root joint, as in getJoint.
        const s32 idx = MR::getModelData(pActor)->findJoint(pName);
        return idx >= 0 ? static_cast< u32 >(idx) : 0;
    }

    const char* getJointName(const LiveActor* pActor, int idx) {
        return MR::getModelData(pActor)->getJointName(idx);
    }

    s32 getJointNum(const LiveActor* pActor) {
        return MR::getModelData(pActor)->getJointCount();
    }

    bool isExistJoint(const LiveActor* pActor, const char* pName) {
        return MR::getModelData(pActor)->findJoint(pName) != -1;
    }

    void copyJointPos(Model* pModel, const char* pName, Vec3f* pPos) {
        MtxPtr jointMtx = MR::getJointMtx(pModel, pName);
        pPos->set< f32 >(jointMtx[0][3], jointMtx[1][3], jointMtx[2][3]);
    }

    void copyJointPos(const LiveActor* pActor, const char* pName, Vec3f* pPos) {
        copyJointPos(MR::getModel(pActor), pName, pPos);
    }

    void copyJointPos(Model* pModel, int idx, Vec3f* pPos) {
        MtxPtr jointMtx = pModel->getAnmMtx(idx);
        pPos->set< f32 >(jointMtx[0][3], jointMtx[1][3], jointMtx[2][3]);
    }

    void copyJointPos(const LiveActor* pActor, int idx, Vec3f* pPos) {
        copyJointPos(MR::getModel(pActor), idx, pPos);
    }

    void copyJointScale(const LiveActor* pActor, const char* pName, Vec3f* pScale) {
        Mtx34f mtx;
        mtx.set(MR::getJointMtx(pActor, pName));
        pScale->x = FloatUtil::sqrt(mtx(2, 0) * mtx(2, 0) + (mtx(0, 0) * mtx(0, 0) + mtx(1, 0) * mtx(1, 0)));
        pScale->y = FloatUtil::sqrt(mtx(2, 1) * mtx(2, 1) + (mtx(0, 1) * mtx(0, 1) + mtx(1, 1) * mtx(1, 1)));
        pScale->z = FloatUtil::sqrt(mtx(2, 2) * mtx(2, 2) + (mtx(0, 2) * mtx(0, 2) + mtx(1, 2) * mtx(1, 2)));
    }

    void hideJoint(Joint* pJoint) {
        for (Material* mat = pJoint->getMesh(); mat != nullptr; mat = mat->mNext) {
            mat->getMesh()->hide();
        }
    }

    void hideJoint(Model* pModel, const char* pName) {
        MR::hideJoint(MR::getJoint(pModel, pName));
    }

    void hideJoint(const LiveActor* pActor, const char* pName) {
        hideJoint(MR::getModel(pActor), pName);
    }

    void hideJointAndChildren(Joint* pJoint) {
        MR::hideJoint(pJoint);

        Joint *j, *i;
        for (i = pJoint->mChild; i != nullptr; i = i->mChild) {
            for (j = i; j != nullptr; j = j->mYounger) {
                MR::hideJointAndChildren(j);
            }
        }
    }

    void hideJointAndChildren(Model* pModel, const char* pName) {
        MR::hideJointAndChildren(MR::getJoint(pModel, pName));
    }

    void hideJointAndChildren(const LiveActor* pActor, const char* pName) {
        hideJointAndChildren(MR::getModel(pActor), pName);
    }

    void showJoint(Joint* pJoint) {
        for (Material* mat = pJoint->getMesh(); mat != nullptr; mat = mat->mNext) {
            mat->getMesh()->show();
        }
    }

    void showJoint(Model* pModel, const char* pName) {
        MR::showJoint(MR::getJoint(pModel, pName));
    }

    void showJoint(const LiveActor* pActor, const char* pName) {
        showJoint(MR::getModel(pActor), pName);
    }

    void showJointAndChildren(Joint* pJoint) {
        MR::showJoint(pJoint);

        Joint *j, *i;
        for (i = pJoint->mChild; i != nullptr; i = i->mChild) {
            for (j = i; j != nullptr; j = j->mYounger) {
                MR::showJointAndChildren(j);
            }
        }
    }

    void showJointAndChildren(Model* pModel, const char* pName) {
        MR::showJointAndChildren(MR::getJoint(pModel, pName));
    }

    void showJointAndChildren(const LiveActor* pActor, const char* pName) {
        showJointAndChildren(MR::getModel(pActor), pName);
    }

    // A plugin's hide (show_joint): the joint's meshes and its children's, on this model only.
    void showJointAndChildrenOn(Model* pModel, Joint* pJoint, bool show) {
        for (Material* mat = pJoint->getMesh(); mat != nullptr; mat = mat->mNext) {
            MeshEntry* pEntry = pModel->getMeshEntry(mat->getMesh()->getIndex());
            if (show) {
                pEntry->offFlag(MeshEntry::Flag_PluginHidden);
            } else {
                pEntry->onFlag(MeshEntry::Flag_PluginHidden);
            }
        }
        for (Joint* pChild = pJoint->mChild; pChild != nullptr; pChild = pChild->mYounger) {
            showJointAndChildrenOn(pModel, pChild, show);
        }
    }

    bool isJointUnder(const Joint* pTop, const Joint* pJoint) {
        if (pTop == pJoint) {
            return true;
        }
        for (const Joint* pChild = pTop->mChild; pChild != nullptr; pChild = pChild->mYounger) {
            if (isJointUnder(pChild, pJoint)) {
                return true;
            }
        }
        return false;
    }

    f32 getJointTransX(const Joint* pJoint) {
        return pJoint->mTransformInfo.mTranslate.x;
    }

    f32 getJointTransY(const Joint* pJoint) {
        return pJoint->mTransformInfo.mTranslate.y;
    }

    f32 getJointTransZ(const Joint* pJoint) {
        return pJoint->mTransformInfo.mTranslate.z;
    }

    Joint* searchChildJoint(Joint* pRoot, Joint* pJoint) {
        if (pRoot == nullptr) {
            return nullptr;
        }

        if (pRoot == pJoint) {
            return reinterpret_cast< Joint* >(-1);
        }

        Joint* pChild = pRoot->mChild;
        if (pChild == pJoint) {
            return pRoot;
        }

        while (pChild != nullptr) {
            pChild = pChild->mYounger;
            if (pChild == pJoint) {
                return pRoot;
            }
        }

        Joint* pParent = searchChildJoint(pRoot->mYounger, pJoint);
        if (pParent != nullptr) {
            return pParent;
        }

        return searchChildJoint(pRoot->mChild, pJoint);
    }

    Joint* getParentJoint(ModelData* pModelData, Joint* pJoint) {
        Joint* pParent = searchChildJoint(pModelData->getJointAt(0), pJoint);
        if (pParent == reinterpret_cast< Joint* >(-1)) {
            return nullptr;
        }

        return pParent;
    }

    Joint* getParentJoint(const LiveActor* pActor, Joint* pJoint) {
        return getParentJoint(getModelData(pActor), pJoint);
    }
};  // namespace MR

