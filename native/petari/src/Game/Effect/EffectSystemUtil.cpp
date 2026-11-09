#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/AutoEffectGroupHolder.hpp"
#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/ParticleCalcExecutor.hpp"
#include "Game/Effect/ParticleDrawExecutor.hpp"
#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Scene/MultiSceneActor.hpp"
#include "Game/Scene/MultiSceneEffectKeeper.hpp"
#include "Game/Screen/PaneEffectKeeper.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include <cstring>

namespace {
    s32 countAnimNames(const char* pNames) {
        s32 count = 1;
        for (const char* p = pNames; *p != '\0'; p++) {
            if (*p == ' ') {
                count++;
            }
        }

        return count;
    }

    void setupMultiEmitter(MultiEmitter* pEmitter, const AutoEffectInfo* pInfo) {
        pEmitter->mAutoInfo = pInfo;
        pEmitter->setDrawOrder(pInfo->mDrawOrder);
        pEmitter->setOffset(pInfo->mOffset);

        if (!MR::isNearZero(pInfo->mScaleValue - 1.0f)) {
            pEmitter->setBaseScale(pInfo->mScaleValue);
        }

        if (!MR::isNearZero(pInfo->mRateValue - 1.0f)) {
            pEmitter->setRate(pInfo->mRateValue, -1);
        }

        if (pInfo->mIsValidPrmColor) {
            Color8 color = pInfo->mPrmColor;
            pEmitter->setGlobalPrmColor(color.r, color.g, color.b, -1);
        }

        if (pInfo->mIsValidEnvColor) {
            Color8 color = pInfo->mEnvColor;
            pEmitter->setGlobalEnvColor(color.r, color.g, color.b, -1);
        }

        if (!MR::isNearZero(pInfo->mLightAffectValue)) {
            pEmitter->mLightAffect = pInfo->mLightAffectValue;
        }
    }
};  // namespace

namespace MR {
    namespace Effect {
        void requestMovementOnAllEmitters() {
            getEffectSystem()->mCalcExec->requestMovementOnPauseIgnore();
            getEffectSystem()->mEmitterHolder->requestMovementOnAllEmitters();
        }

        void requestMovementOffAllLoopEmitters() {
            getEffectSystem()->mEmitterHolder->requestMovementOffAllLoopEmitters();
        }

        void checkEffectSceneUpdate(const EffectSystem* pSystem) {
            pSystem->mCalcExec->movementCheckUpdate();
        }

        void movementEffectNormal(const EffectSystem* pSystem) {
            pSystem->mCalcExec->movementNormal();
        }

        void drawEffect3D(const EffectSystem* pSystem, const Pos3f& rViewMtx) {
            pSystem->mDrawExec->drawWithViewMtx3D(rViewMtx);
        }

        void drawEffect2D(const EffectSystem* pSystem) {
            pSystem->mDrawExec->draw2D();
        }

        void forceDeleteAllEmitters(const EffectSystem* pSystem) {
            pSystem->mEmitterHolder->forceDeleteAllEmitters();
        }

        bool isExistInResource(u16* pIndex, const char* pName) {
            return getParticleResourceHolder()->isExistInResource(pName, pIndex);
        }

        bool isEffect2D(const MultiEmitter* pEmitter) {
            if (pEmitter->mAutoInfo == nullptr) {
                return false;
            }

            return pEmitter->mAutoInfo->mDrawOrder == 6 || pEmitter->mAutoInfo->mDrawOrder == 7;
        }

        void initEffectSyncBck(EffectKeeper* pKeeper, const ModelManager* pModelManager, const char* pEffectName, const char* pAnimName,
                               s32 animCount, f32 startFrame, f32 endFrame) {
            // A mod's model with no animations: the effect is played by name.
            if (pModelManager->mXanimePlayer == nullptr) {
                return;
            }
            pKeeper->registerSyncBckEffect(pModelManager->mXanimePlayer, pEffectName, pAnimName, animCount, startFrame, endFrame);
        }

        void addEffectSyncBck(MultiEmitter* pEmitter, const ModelManager* pModelManager, const char* pAnimName) {
            if (pModelManager->mXanimePlayer != nullptr) {
                pEmitter->addSyncBck(pModelManager->mXanimePlayer, pAnimName);
            }
        }

        int getAutoEffectNum(const char* pName) {
            return getParticleResourceHolder()->getAutoEffectNum(pName);
        }

        Table* getAutoEffectListBinary() {
            return getParticleResourceHolder()->getAutoEffectListBinary();
        }

        void setupMultiEmitter(EffectKeeper* pKeeper, const ModelManager* pModelManager, const AutoEffectInfo* pInfo) {
            MultiEmitter* pEmitter = pKeeper->getEmitter(pInfo->getName());
            setupMultiEmitterSyncBck(pKeeper, pModelManager, pInfo);
            ::setupMultiEmitter(pEmitter, pInfo);
            pEmitter->scanParticleEmitter(getEffectSystem());

            if (pInfo->mParentName != nullptr) {
                pKeeper->getEmitter(pInfo->mParentName)->addChildEmitter(pEmitter);
            }
        }

        void setupMultiEmitterSyncBck(EffectKeeper* pKeeper, const ModelManager* pModelManager, const AutoEffectInfo* pInfo) {
            if (pInfo->mAnimName == nullptr) {
                return;
            }

            MultiEmitter* pEmitter = pKeeper->getEmitter(pInfo->getName());
            if (!hasStringSpace(pInfo->mAnimName)) {
                const char* pAnimName = pInfo->mAnimName;
                initEffectSyncBck(pKeeper, pModelManager, pInfo->getName(), pAnimName, 1, pInfo->mStartFrame, pInfo->mEndFrame);
            } else {
                const char* const pAnimNames = pInfo->mAnimName;
                u32 start = 0;
                const u32 length = static_cast< u32 >(strlen(pAnimNames));
                for (u32 i = 0; i <= length; i++) {
                    if (pAnimNames[i] == ' ' || pAnimNames[i] == '\0') {
                        const FormattedName name("%.*s", static_cast< int >(i - start), pAnimNames + start);
                        const char* pAnimName = nullptr;
                        findBckNameStringInResource(&pAnimName, pModelManager->getResourceHolder(), name.c_str());
                        if (pEmitter->mSyncBck == nullptr) {
                            initEffectSyncBck(pKeeper, pModelManager, pInfo->getName(), pAnimName, ::countAnimNames(pAnimNames), 0.0f, -1.0f);
                        } else {
                            addEffectSyncBck(pEmitter, pModelManager, pAnimName);
                        }

                        start = i + 1;
                    }
                }
            }

            pEmitter->setContinueBckEnd((pInfo->mFlag & AutoEffectInfo::FLAG_CONTINUE_ANIM_END) != 0);
        }

        void registerAutoEffectInfoGroup(EffectKeeper* pKeeper, const LiveActor* pActor, const char* pName) {
            createAndAddAutoEffectGroup(getEffectSystem()->mGroupHolder, pName);
            registerAutoEffectInfos(getEffectSystem()->mGroupHolder, pKeeper, pActor, pName);
        }

        void requestMovementOn(EffectKeeper* pKeeper) {
            for (s32 i = 0; i < pKeeper->mEmitters.size(); i++) {
                MultiEmitter* pEmitter = pKeeper->getEmitter(i);
                if (pEmitter != nullptr && pEmitter->isValid()) {
                    pEmitter->pauseOff(-1);
                }
            }
        }

        void registerAutoEffectInfoGroup(PaneEffectKeeper* pKeeper, const LayoutActor* pActor, const char* pName) {
            registerAutoEffectInfoGroup(pKeeper, getEffectSystem(), pActor, pName);
        }

        void registerAutoEffectInfoGroup(PaneEffectKeeper* pKeeper, const EffectSystem* pSystem, const LayoutActor* pActor, const char* pName) {
            createAndAddAutoEffectGroup(pSystem->mGroupHolder, pName);
            registerAutoEffectInfos(pSystem->mGroupHolder, pKeeper, pActor, pName);
        }

        void addAutoEffect(EffectKeeper* pKeeper, const LiveActor* pActor, const AutoEffectInfo* pInfo) {
            if (pInfo->mJointName != nullptr) {
                const char* pEffectName = pInfo->mEffectName;
                pKeeper->registerEffect(pEffectName, getJointMtx(pActor, pInfo->mJointName), pInfo->mUniqueName);
            } else if (pActor->getBaseMtx() != nullptr) {
                const char* pEffectName = pInfo->mEffectName;
                pKeeper->registerEffect(pEffectName, pActor->getBaseMtx(), &pActor->mScale, pInfo->mUniqueName);
            } else {
                pKeeper->registerEffect(pInfo->mEffectName, &pActor->mPosition, &pActor->mRotation, &pActor->mScale, pInfo->mUniqueName);
            }

            setupMultiEmitter(pKeeper, pActor->mModelManager, pInfo);
        }

        void addAutoEffect(PaneEffectKeeper* pKeeper, const LayoutActor* pActor, const AutoEffectInfo* pInfo) {
            pKeeper->add(pInfo->mJointName, pInfo->mEffectName, pInfo->mUniqueName);
            ::setupMultiEmitter(pKeeper->getEmitter(pInfo->mUniqueName), pInfo);
        }

        void addAutoEffect(MultiSceneEffectKeeper* pKeeper, const MultiSceneActor* pActor, const AutoEffectInfo* pInfo) {
            if (pInfo->mJointName != nullptr) {
                const char* pEffectName = pInfo->mEffectName;
                pKeeper->add(pEffectName, MultiScene::getJointMtx(pActor, pInfo->mJointName), pInfo->mUniqueName);
            } else {
                pKeeper->add(pInfo->mEffectName, &pActor->mTranslation, &pActor->mRotation, &pActor->mScale, pInfo->mUniqueName);
            }

            ::setupMultiEmitter(pKeeper->get(pInfo->mUniqueName), pInfo);
        }

        void registerAutoEffectInfoGroup(MultiSceneEffectKeeper* pKeeper, const EffectSystem* pSystem, const MultiSceneActor* pActor,
                                         const char* pName) {
            createAndAddAutoEffectGroup(pSystem->mGroupHolder, pName);
            registerAutoEffectInfos(pSystem->mGroupHolder, pKeeper, pActor, pName);
        }

        void deleteParticleEmitter(ParticleEmitter* pEmitter) {
            if (pEmitter->mEmitter != nullptr) {
                pEmitter->mEmitter->playCalcEmitter();
                pEmitter->mEmitter->becomeInvalidEmitter();
            }
        }

        void setLinkSingleEmitter(ParticleEmitter* pEmitter, SingleEmitter* pSingleEmitter) {
            pEmitter->mEmitter->setUserWork(reinterpret_cast< uintptr_t >(pSingleEmitter));
        }

        SingleEmitter* getLinkSingleEmitter(const EmitterCore* pEmitter) {
            return reinterpret_cast< SingleEmitter* >(pEmitter->getUserWork());
        }

        void forceDeleteAllOneTimeEmitter() {
            getEffectSystem()->mEmitterHolder->forceDeleteAllOneTimeEmitters();
        }

        void createParticleEmitter(ParticleEmitter* pEmitter, EmitterManager* pManager, const Vec3f& rPosition, u16 resourceId, u8 groupId,
                                   u8 resourceManagerId) {
            EmitterCore* pBaseEmitter = pManager->createSimpleEmitterID(rPosition, resourceId, groupId, resourceManagerId, nullptr, nullptr);
            if (pBaseEmitter != nullptr) {
                pEmitter->mEmitter = pBaseEmitter;
                pEmitter->init();
            }
        }

        bool isExistInResource(u16* pIndex, const char* pName, s32 number) {
            MR::FormattedName name("%s%02d", pName, number);
            return isExistInResource(pIndex, name.c_str());
        }

        const char* getEffectAttributeName(s32 floorCode) {
            switch (floorCode) {
            case 20:
            case 21:
            case 22:
            case 23:
                return "Water";
            case 13:
            case 30:
                return "Sand";
            case 5:
                return "Ice";
            case 10:
                return "DamageFire";
            case 31:
            case 32:
                return "Mud";
            default:
                return "Default";
            }
        }

        AutoEffectInfo* createAutoEffect(const char* pUniqueName) {
            Table* pInfo = getAutoEffectListBinary();
            TableIter iter = pInfo->findElement("UniqueName", pUniqueName, 0);
            AutoEffectInfo* pEffectInfo = new AutoEffectInfo();
            pEffectInfo->init(iter);
            return pEffectInfo;
        }
    };  // namespace Effect
};  // namespace MR
