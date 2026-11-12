#include "Game/Player/MarioEffect.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleMath.hpp"
#include <cstdio>
#include "Game/Util/StringUtil.hpp"

// A table's flags, written 0xAABBCCDD: AA first. By value, on any machine.
struct EffectFlagWord {
    EffectFlagWord(u32 word) : mWord(word) {
    }

    u8 byte0() const {
        return static_cast< u8 >(mWord >> 24);
    }

    u8 byte1() const {
        return static_cast< u8 >(mWord >> 16);
    }

    u8 byte2() const {
        return static_cast< u8 >(mWord >> 8);
    }

    u32 mWord;
};

struct MaterialEffectEntry {
    const char* mName;
    EffectFlagWord mFlag;
    const char* mEffects[7];
    const char* mCurrent;
    MultiEmitter* mEmitter;
};

struct SmokeEffectEntry {
    const char* mName;
    EffectFlagWord mType;
    f32 mScale;
    f32 mRate;
    EffectFlagWord mFlags;
    u16 mInterval;
    u16 mTimer;
    u32 mHash;
    MultiEmitter* mEmitter;
    u8 mMaterial;
};

MaterialEffectEntry cMaterialEffectTable[] = {
    {"属性尻ドロップ",
     0x00000000,
     {"HipDropSmoke", "HipDropWater", "HipDropFlower", "HipDropSand", "HipDropSnow", "MudCircle", "HoneyCircle"},
     nullptr,
     nullptr},
    {"属性ハチ風", 0x03000000, {"Hovering", "HoveringWater", "Hovering", "Hovering", "Hovering", "Hovering", "Hovering"}, nullptr, nullptr},
    {"属性スピン", 0x00000000, {nullptr, "WaterSpin", "FlowerSpin", "SandSpin", "SnowSpin", "MudCircle", "HoneyCircle"}, nullptr, nullptr},
    {"属性ステージイン",
     0x00000000,
     {"StageStartGroundSmoke", nullptr, nullptr, "StageStartGroundSand", nullptr, nullptr, nullptr},
     nullptr,
     nullptr},
    {nullptr, 0, {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, nullptr, nullptr}};

namespace {
    // The sound when health or air is low.
    const bool cLowHealthWarning = true;

    void addMarioEffectAtJoint(LiveActor* pActor, const char* pEffectName, const char* pJointName, const char* pGroupName) {
        MtxPtr mtx = pJointName != nullptr ? MR::getJointMtx(pActor, pJointName) : pActor->getBaseMtx();
        pActor->mEffectKeeper->registerEffect(pEffectName, mtx, &pActor->mScale, pGroupName);
    }

};  // namespace

void MarioEffect::execute(EmitterCore* pEmitter) {
    for (s32 i = 0; i < mFollowCount; i++) {
        if (mFollows[i].mEmitter == pEmitter) {
            updateFollowMtx(&mFollows[i]);
            return;
        }
    }
}

bool MarioEffect::isFollowing(const MovingFollowMtx& rFollow) const {
    return rFollow.mEmitter != nullptr && rFollow.mEmitter->getEmitterCallBackPtr() == static_cast< const EmitterCallBack* >(this) &&
           !rFollow.mEmitter->checkStatus(0x200);
}

EmitterCore* MarioEffect::addRequest(const char* pEffectName, MtxPtr pMtx) {
    s32 requestIndex = mFollowCount;
    for (s32 i = 0; i < mFollowCount; i++) {
        if (!isFollowing(mFollows[i])) {
            mFollows[i].mEmitter = nullptr;
            if (requestIndex == mFollowCount) {
                requestIndex = i;
            }
        }
    }
    if (requestIndex == mFollowCount) {
        mFollows.at(mFollowCount++).mEmitter = nullptr;
    }

    MovingFollowMtx* target = &mFollows[requestIndex];

    MultiEmitter* emitter = MR::emitEffectWithEmitterCallBack(mActor, pEffectName, this);
    if (emitter->getParticleEmitter(0) == nullptr) {
        return nullptr;
    }

    EmitterCore* baseEmitter = emitter->getParticleEmitter(0)->mEmitter;
    target->mEmitter = baseEmitter;

    for (s32 i = 0; i < mFollowCount; i++) {
        if (requestIndex != i && mFollows[i].mEmitter == target->mEmitter) {
            mFollows[i].mEmitter = nullptr;
        }
    }

    target->mFrame = 0;
    target->mBaseMtx = pMtx;
    return target->mEmitter;
}

void MarioEffect::updateFollowMtx(MovingFollowMtx* pFollow) {
    if (pFollow->mEmitter->isEnableDeleteEmitter()) {
        pFollow->mEmitter = nullptr;
        return;
    }

    if (pFollow->mFrame == 0) {
        Mtx temp;
        NebulaMtxCopy(mCommonEffectMtx, temp);

        const Vec3f& rTrans = getTrans();
        MR::setMtxTrans(temp, rTrans.x, rTrans.y, rTrans.z);

        if (pFollow->mBaseMtx == nullptr) {
            pFollow->mBaseMtx = reinterpret_cast< MtxPtr >(getPlayer()->getMoveBaseMtx());
            if (pFollow->mBaseMtx == nullptr) {
                return;
            }
        }

        Mtx inv;
        NebulaMtxInverse(pFollow->mBaseMtx, inv);
        MR::multMtx(pFollow->mLocalMtx, temp, inv);
        NebulaMtxCopy(temp, pFollow->mWorldMtx);
    } else {
        MR::multMtx(pFollow->mWorldMtx, pFollow->mLocalMtx, pFollow->mBaseMtx);
    }

    pFollow->mEmitter->setGlobalRTMatrix(pFollow->mWorldMtx);

    pFollow->mFrame++;
    if (pFollow->mFrame == 2) {
        pFollow->mEmitter->playCalcEmitter();
        pFollow->mEmitter->becomeInvalidEmitter();
    }
}

void MarioActor::initMaterialEffect() {
    MaterialEffectEntry* entry = cMaterialEffectTable;
    u32 entryCount = 0;
    while (entry->mName != nullptr) {
        for (u32 i = 0; i < 7; i++) {
            if (entry->mEffects[i] == nullptr) {
                continue;
            }

            if (!MR::isRegisteredEffect(this, entry->mEffects[i])) {
                mEffectKeeper->registerEffectWithoutSRT(entry->mEffects[i], nullptr);
            } else {
                MR::setEffectHostSRT(this, entry->mEffects[i], nullptr, nullptr, nullptr);
            }
        }

        entry->mCurrent = nullptr;
        entryCount++;
        entry->mEmitter = nullptr;
        entry++;
    }

    mMaterialEffects.reset(entryCount);
    for (u32 i = 0; i < entryCount; i++) {
        mMaterialEffects.add(cMaterialEffectTable[i].mName, i);
    }
}

s32 MarioActor::getFloorMaterialIndex(u32 flags) const {
    s32 materialIndex = 0;

    if (mMarioEffect->mCubeArg1 == 0) {
        return 2;
    }

    if (mFloorMaterialHold != 0) {
        return 1;
    }

    s32 floor = mMario->mFloorCode;
    if ((floor == 0x0D || floor == 0x1E) && !mMario->isCurrentFloorSand()) {
        floor = mMario->mPolygonFloorCode;
    }

    if (mMario->isStatusActive(MarioStatus_Wall)) {
        if (mMario->mMovementStates.frontWall) {
            floor = mMario->mWallFloorCodes[0];
        } else if (mMario->mMovementStates.backWall) {
            floor = mMario->mWallFloorCodes[1];
        } else if (mMario->mMovementStates.sideWall) {
            floor = mMario->mWallFloorCodes[2];
        }
    }

    if (mMario->mDrawStates.inWater) {
        floor = 0x17;
    }

    if (mMario->mDrawStates.inShallowWater) {
        floor = 0x17;
    }

    if (flags & 0x2) {
        if (mMario->mDrawStates.onWaterFloor) {
            floor = 0x17;
        }
    }

    if (mMario->mWalkingResist != 0.0f) {
        floor = 0x1A;
    }

    if (mMario->isPlayerModeBee() && MR::isWallCodeNoAction(mMario->mGroundPolygon)) {
        return -1;
    }

    switch (floor) {
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
        materialIndex = 1;
        break;
    case 0x0E:
    case 0x21:
        return -1;
    case 0x0D:
    case 0x1E:
        materialIndex = 3;
        break;
    case 0x1A:
        materialIndex = 4;
        break;
    case 0x20:
        materialIndex = 5;
        if (MR::getSoundCodeIndex(mMario->mShadowPolygon) == CollisionSoundCode_Honey) {
            materialIndex = 6;
        }

        break;
    case 0x1B:
    case 0x1C:
        if (MR::getSoundCodeIndex(mMario->mShadowPolygon) == CollisionSoundCode_Sand) {
            materialIndex = 3;
        }

        break;
    default:
        break;
    }

    if (mMario->mDrawStates.inWater || mMario->mDrawStates.inShallowWater) {
        materialIndex = 1;
    }

    if (!mMario->mMovementStates.onGround) {
        if (mMario->mSwim->mDeepWater != 0) {
            if (mMario->mSwim->mSurfaceDepth < 400.0f) {
                materialIndex = 1;
            }
        }
    }

    return materialIndex;
}

MultiEmitter* MarioActor::playMaterialEffect(const char* pName) {
    const u32 index = mMaterialEffects.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return nullptr;
    }
    MaterialEffectEntry* entry = &cMaterialEffectTable[index];

    const s32 materialIndex = getFloorMaterialIndex(entry->mFlag.byte0());
    if (materialIndex == -1) {
        return nullptr;
    }

    const u32 materialOffset = static_cast< u32 >(materialIndex);
    const char* effectName = entry->mEffects[materialOffset];
    if (effectName == nullptr) {
        return nullptr;
    }

    if (entry->mFlag.byte0() == 1) {
        const char* current = entry->mCurrent;
        if (current != nullptr && current == effectName) {
            return entry->mEmitter;
        }
    }

    MultiEmitter* emitter = MR::emitEffect(this, effectName);

    const char* current = entry->mCurrent;
    if (current != nullptr && current != entry->mEffects[materialOffset]) {
        MR::deleteEffect(this, current);
    }

    entry->mCurrent = entry->mEffects[materialOffset];
    entry->mEmitter = emitter;

    if (emitter != nullptr) {
        MarioEffect* marioEffect = mMarioEffect;
        if (marioEffect->mCubeColorOn) {
            emitter->setGlobalPrmColor(marioEffect->mCubePrmColor.r, marioEffect->mCubePrmColor.g, marioEffect->mCubePrmColor.b, -1);
            emitter->setGlobalEnvColor(marioEffect->mCubeEnvColor.r, marioEffect->mCubeEnvColor.g, marioEffect->mCubeEnvColor.b, -1);
        } else {
            emitter->setGlobalPrmColor(0xFF, 0xFF, 0xFF, -1);
            emitter->setGlobalEnvColor(0xFF, 0xFF, 0xFF, -1);
        }
    }

    return emitter;
}

void MarioActor::stopMaterialEffect(const char* pName) {
    const u32 index = mMaterialEffects.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return;
    }
    MaterialEffectEntry* entry = &cMaterialEffectTable[index];

    if (entry->mCurrent != nullptr) {
        MR::deleteEffect(this, entry->mCurrent);
        entry->mCurrent = nullptr;
        entry->mEmitter = nullptr;
    }
}

SmokeEffectEntry cSmokeTable[] = {{"共通着地普通", 0x08000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通着地大", 0x03000000, 1.5f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通壁ジャンプ", 0x03000000, 1.0f, 1.0f, 0x00000300, 0, 0, 0, nullptr, 0},
                                  {"共通砂煙レベル", 0x07000000, 0.65f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0},
                                  {"共通壁手擦り", 0x01000000, 0.35f, 1.0f, 0x02010000, 0, 0, 0, nullptr, 0},
                                  {"共通壁上昇", 0x01000000, 1.0f, 1.0f, 0x00000200, 0, 0, 0, nullptr, 0},
                                  {"共通スリップ坂", 0x02000000, 0.35f, 1.0f, 0x00010000, 2, 0, 0, nullptr, 0},
                                  {"共通スリップ空転", 0x00000000, 1.0f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0},
                                  {"共通スリップ坂制動", 0x02000000, 0.5f, 1.0f, 0x00000000, 2, 0, 0, nullptr, 0},
                                  {"共通壁ヒット着地", 0x03000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通ダメージ着地", 0x03000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通地上スピン", 0x03000000, 0.9f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通ブレーキ", 0x02000000, 0.65f, 1.0f, 0x00010000, 15, 0, 0, nullptr, 0},
                                  {"共通ハイジャンプ", 0x02000000, 0.65f, 1.0f, 0x00010000, 6, 0, 0, nullptr, 0},
                                  {"共通ブラックホール", 0x01000000, 0.65f, 1.0f, 0x03030000, 0, 0, 0, nullptr, 0},
                                  {"共通引き戻し着地", 0x03000000, 1.2f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通水底接触", 0x01000000, 6.0f, 0.5f, 0x00000400, 0, 0, 0, nullptr, 0},
                                  {"共通跳躍", 0x08000000, 1.0f, 1.0f, 0x00000C00, 0, 0, 0, nullptr, 0},
                                  {"共通ひこうき雲", 0x03000000, 0.3f, 0.2f, 0x00010000, 0, 0, 0, nullptr, 0},
                                  {"共通ひこうきブースト", 0x00000000, 2.0f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0},
                                  {nullptr, 0, 0.0f, 0.0f, 0, 0, 0, 0, nullptr, 0}};

void MarioActor::initCommonEffect() {
    mCommonEffectCount = 0;
    mSmokeEntryCount = 0;

    // The list holds the table's entries of types 2 and 5: the ones that time out.
    s32 timedNum = 0;
    for (const SmokeEffectEntry* pEntry = cSmokeTable; pEntry->mName != nullptr; pEntry++) {
        if (pEntry->mType.byte0() == 2 || pEntry->mType.byte0() == 5) {
            timedNum++;
        }
    }
    mSmokeEntries = new SmokeEffectEntry*[timedNum];

    SmokeEffectEntry* entry = cSmokeTable;
    while (entry->mName != nullptr) {
        const char* smokeEffects[] = {"SmokeSphere", "SmokeSphereLoop", "SmokeCircle", "SmokeCircleLoop"};
        const char* waterEffects[] = {"WaterSphere", "WaterSphereLoop", "WaterCircle", "WaterCircleLoop"};
        const char* flowerEffects[] = {"FlowerCircle", "FlowerSphereLoop", "FlowerCircle", "FlowerSphereLoop"};
        const char* sandEffects[] = {"SandCircle", "SandSphereLoop", "SandCircle", "SandSphereLoop"};
        const char* snowEffects[] = {"SnowSphere", "SnowSphereLoop", "SnowCircle", "SnowCircleLoop"};
        const char* smokeFollowEffects[] = {"SmokeSphereFollow", "SmokeSphereFollow1Time", "SmokeCircleFollow", "SmokeCircleLoopFollow"};
        const char* mudEffects[] = {"MudSphere", "MudSphereLoop", "MudCircle", "MudCircleLoop"};
        const char* honeyEffects[] = {"HoneySphere", "HoneySphereLoop", "HoneyCircle", "HoneyCircleLoop"};

        s32 variant = 0;
        s32 useFollow = 0;

        switch (entry->mType.byte0()) {
        case 6:
            useFollow = 1;
        case 0:
            variant = 0;
            break;
        case 7:
            useFollow = 1;
        case 1:
        case 2:
            variant = 1;
            break;
        case 8:
            useFollow = 1;
        case 3:
            variant = 2;
            break;
        case 9:
            useFollow = 1;
        case 4:
        case 5:
            variant = 3;
            break;
        }

        // A material's effect is named by the entry's name from its materialIndex'th byte: the table's strings, which
        // the keeper holds by pointer.
        const char* const name = entry->mName;
        for (u32 materialIndex = 0; materialIndex < 7; materialIndex++) {
            const u8 flag2 = entry->mFlags.byte2();
            if ((flag2 & 0x8) && materialIndex != 1) {
                continue;
            }

            const char* const* effectList = smokeEffects;
            switch (materialIndex) {
            case 0:
                effectList = useFollow ? smokeFollowEffects : smokeEffects;
                break;
            case 1:
                effectList = waterEffects;
                break;
            case 2:
                effectList = flowerEffects;
                break;
            case 3:
                effectList = sandEffects;
                break;
            case 4:
                effectList = snowEffects;
                break;
            case 5:
                effectList = mudEffects;
                break;
            case 6:
                effectList = honeyEffects;
                break;
            }

            if (flag2 != 0) {
                const char* effectName = effectList[variant];
                mEffectKeeper->registerEffectWithoutSRT(effectName, name + materialIndex);
            } else {
                const u8 flag0 = entry->mFlags.byte0();
                switch (flag0) {
                case 0: {
                    const char* effectName = effectList[variant];
                    const char* hostName = name + materialIndex;
                    mEffectKeeper->registerEffectWithoutSRT(effectName, hostName);
                    const Vec3f* rot = &mRotation;
                    const Vec3f* pos = &mPosition;
                    MR::setEffectHostSRT(this, hostName != nullptr ? hostName : effectName, pos, rot, nullptr);
                    break;
                }

                case 1:
                    ::addMarioEffectAtJoint(this, effectList[variant], "FootR", name + materialIndex);
                    break;
                case 2:
                    ::addMarioEffectAtJoint(this, effectList[variant], "HandR", name + materialIndex);
                    break;
                case 3:
                    ::addMarioEffectAtJoint(this, effectList[variant], "Spine1", name + materialIndex);
                    break;
                default:
                    break;
                }
            }

            if ((entry->mFlags.byte1() & 0x1) != 0) {
                MR::getEffect(this, name + materialIndex)->forceFollowOn();
            }

            if ((entry->mFlags.byte1() & 0x2) != 0) {
                MR::getEffect(this, name + materialIndex)->forceScaleOn();
            }
        }

        entry->mTimer = 0;
        switch (entry->mType.byte0()) {
        case 2:
        case 5: {
            mSmokeEntries[mSmokeEntryCount] = entry;
            mSmokeEntryCount++;
            break;
        }

        default:
            entry->mInterval = 0;
            break;
        }

        entry->mHash = MR::getHashCode(entry->mName);
        entry->mEmitter = nullptr;
        entry->mMaterial = 0;
        entry++;
        mCommonEffectCount++;
    }
}

MultiEmitter* MarioActor::playCommonEffect(const char* pName) {
    if (mPlayerMode == 3) {
        return nullptr;
    }

    const u32 hash = MR::getHashCode(pName);
    for (u32 i = 0; i < mCommonEffectCount; i++) {
        SmokeEffectEntry* entry = &cSmokeTable[i];
        if (entry->mHash == hash) {
            MultiEmitter* emitter = nullptr;
            MarioEffect* marioEffect;
            s32 materialIndex = getFloorMaterialIndex(0);

            if (materialIndex == -1) {
                return nullptr;
            }

            if ((entry->mFlags.byte2() & 0x8) && materialIndex != 1) {
                return nullptr;
            }

            switch (entry->mType.byte0()) {
            case 1:
            case 2:
            case 4:
            case 5:
                emitter = entry->mEmitter;
                break;
            case 7:
            case 9:
                if (materialIndex != 0) {
                    emitter = entry->mEmitter;
                }

                break;
            default:
                break;
            }

            if (emitter != nullptr && materialIndex != entry->mMaterial) {
                emitter->deleteEmitter();
                emitter = nullptr;
            }

            if (emitter == nullptr) {
                s32 needsFollow = 0;
                s32 needsSRT = 0;

                if (materialIndex == 0) {
                    if (entry->mType.byte0() >= 6) {
                        const Triangle* tri = mMario->getGroundPolygon();
                        MtxPtr prevMtx = tri->getPrevBaseMtx()->toMtxPtr();
                        MtxPtr baseMtx = mMario->getGroundPolygon()->getBaseMtx()->toMtxPtr();

                        if (!MR::isSameMtx(baseMtx, prevMtx)) {
                            needsFollow = 1;
                        }

                        needsSRT = 1;
                    }
                }

                if (needsFollow) {
                    const Triangle* tri = mMario->getGroundPolygon();
                    EmitterCore* baseEmitter = mMarioEffect->addRequest(pName + materialIndex, tri->getBaseMtx()->toMtxPtr());

                    if (baseEmitter != nullptr) {
                        baseEmitter->setGlobalScale(Vec3f(entry->mScale, entry->mScale, entry->mScale));
                    }
                } else {
                    if (needsSRT) {
                        const u8 hostFlag = entry->mFlags.byte1();
                        const Vec3f* hostPos = nullptr;
                        const Vec3f* hostRot = nullptr;
                        const Vec3f* hostScale = nullptr;

                        if (hostFlag & 0x1) {
                            hostPos = &mPosition;
                        }

                        if (hostFlag & 0x4) {
                            hostRot = &mRotation;
                        }

                        if (hostFlag & 0x2) {
                            hostScale = &mScale;
                        }

                        MR::setEffectHostSRT(this, pName + materialIndex, hostPos, hostRot, hostScale);
                    }

                    emitter = MR::emitEffect(this, pName + materialIndex);
                }
            }

            if (!(entry->mFlags.byte1() & 0x2)) {
                if (emitter != nullptr) {
                    emitter->setGlobalScale(entry->mScale, -1);
                }
            }

            if (emitter != nullptr) {
                emitter->setRate(entry->mRate, -1);
            }

            if (entry->mFlags.byte2() & 0x1) {
                Vec3f dir(mMario->getWallNorm());
                Pos3f mtx;
                Vec3f rot;
                MR::makeMtxUpNoSupportPos(&mtx, dir, Vec3f(0.0f, 0.0f, 0.0f));
                MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

                if (emitter != nullptr) {
                    emitter->setGlobalRotationDegree(rot, -1);
                }

                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(mPosition, -1);
                }
            } else {
                Vec3f dir(mMario->mGroundNorm);
                Pos3f mtx;
                Vec3f rot;
                MR::makeMtxUpNoSupportPos(&mtx, dir, Vec3f(0.0f, 0.0f, 0.0f));
                MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);
                NebulaMtxCopy(mtx.toMtxPtr(), mMarioEffect->mCommonEffectMtx.toMtxPtr());

                if (emitter != nullptr) {
                    emitter->setGlobalRotationDegree(rot, -1);
                }

                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(mPosition, -1);
                }
            }

            if (entry->mFlags.byte2() & 0x2) {
                Vec3f trans(mMario->mFrontWallPos);
                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(trans, -1);
                }
            }

            if (entry->mInterval != 0) {
                entry->mTimer = entry->mInterval;
            }

            switch (entry->mType.byte0()) {
            case 1:
            case 2:
            case 4:
            case 5:
                entry->mEmitter = emitter;
                entry->mMaterial = static_cast< u8 >(materialIndex);
                break;
            case 7:
            case 9:
                if (materialIndex != 0) {
                    entry->mEmitter = emitter;
                }

                entry->mMaterial = static_cast< u8 >(materialIndex);
                break;
            default:
                break;
            }

            if (emitter != nullptr) {
                marioEffect = mMarioEffect;
                if (marioEffect->mCubeColorOn) {
                    emitter->setGlobalPrmColor(marioEffect->mCubePrmColor.r, marioEffect->mCubePrmColor.g, marioEffect->mCubePrmColor.b, -1);
                    emitter->setGlobalEnvColor(marioEffect->mCubeEnvColor.r, marioEffect->mCubeEnvColor.g, marioEffect->mCubeEnvColor.b, -1);
                } else {
                    emitter->setGlobalPrmColor(0xFF, 0xFF, 0xFF, -1);
                    emitter->setGlobalEnvColor(0xFF, 0xFF, 0xFF, -1);
                }

                if (materialIndex == 1) {
                    emitter->setDrawOrder(3);
                }
            }

            return emitter;
        }
    }

    return nullptr;
}

void MarioActor::initEffect() {
    mEffectFlags = EffectFlags();
    initEffectKeeper(0xB8, "Mario");
    initCommonEffect();
    initMaterialEffect();

    MR::setEffectName(this, "SlipUp", "スリップアップ");
    MR::setEffectName(this, "WallSpark", "壁スパーク");
    MR::setEffectName(this, "Damage", "ダメージ");
    MR::setEffectName(this, "StepOn", "ふみつぶし");
    MR::setEffectName(this, "SuperSpinDriverEnd", "スーパースピンドライバ終了");
    MR::setEffectName(this, "BringBubble", "引き戻し泡");
    MR::setEffectName(this, "BringBubbleBreak", "引き戻し泡破裂");
    MR::setEffectName(this, "WaterSplashRight", "水はね右");
    MR::setEffectName(this, "WaterSplashLeft", "水はね左");
    MR::setEffectName(this, "WaterSplashSRight", "水はね右弱");
    MR::setEffectName(this, "WaterSplashSLeft", "水はね左弱");
    MR::setEffectName(this, "SpinLightBurst", "スピンライト消去");
    MR::setEffectName(this, "ElectricDamage", "ビリビリ");
    MR::setEffectName(this, "FireDamageSmoke", "炎ダメージ煙");
    MR::setEffectName(this, "BlueFireDamageSmoke", "炎ダメージ青煙");
    MR::setEffectName(this, "SpinLight", "スピンライト");
    MR::setEffectName(this, "IceMarioSpinLight", "アイススピン");
    MR::setEffectName(this, "FireMarioSpinLight", "ファイアスピン");
    MR::setEffectName(this, "BeeMarioSpinLight", "ハチスピン");
    MR::setEffectName(this, "BeeLuigiSpinLight", "ハチルイージスピン");
    MR::setEffectName(this, "Sweat", "いい汗");
    MR::setEffectName(this, "CarryStartShort", "ひろいクイック");
    MR::setEffectName(this, "AirRecover", "酸素回復");
    MR::setEffectName(this, "Invincible", "無敵中");
    MR::setEffectHostSRT(this, "無敵中", &mPosition, nullptr, nullptr);
    MR::setEffectName(this, "Ice", "アイス中");
    MR::setEffectHostSRT(this, "アイス中", &mPosition, nullptr, nullptr);
    MR::setEffectName(this, "FireBallThrow", "ファイアボール投げ");
    MR::setEffectName(this, "Throw", "こうら投げ");
    MR::setEffectName(this, "DieBuryBody", "砂埋まり体");
    MR::setEffectName(this, "DieBuryHand", "砂埋まり手");
    MR::setEffectName(this, "DieDeathMudBody", "泥埋まり体");
    MR::setEffectName(this, "DieDeathMudHand", "泥埋まり手");
    MR::setEffectName(this, "SpinLightConcentrate", "スピン許可");
    MR::setEffectName(this, "FooMarioHandGlowL", "フーマリオグロー左");
    MR::setEffectName(this, "FooMarioHandGlowR", "フーマリオグロー右");
    MR::setEffectName(this, "FooMarioBrakeLampL", "フーマリオブレーキ左");
    MR::setEffectName(this, "FooMarioBrakeLampR", "フーマリオブレーキ右");
    MR::setEffectName(this, "FooMarioEndL", "フーマリオ解除左");
    MR::setEffectName(this, "FooMarioEndR", "フーマリオ解除右");
    MR::setEffectName(this, "WallHit", "壁ヒット");
    MR::setEffectName(this, "BarrierBound", "結界ヒット");
    MR::setEffectName(this, "WaterBound", "水壁ヒット");
    MR::setEffectName(this, "IceWallJump", "氷壁ジャンプ");
    MR::setEffectHostSRT(this, "氷壁ジャンプ", nullptr, nullptr, nullptr);
    MR::setEffectName(this, "CeilingCrash", "天井ヒット");
    MR::setEffectName(this, "Coin", "コイン");
    MR::setEffectName(this, "PunchHit", "パンチヒット");
    MR::setEffectName(this, "WaterRipple", "水波紋");
    MR::setEffectName(this, "SwimWaitRipple", "水面ウエイト波紋");
    MR::setEffectHostSRT(this, "水面ウエイト波紋", &mWaterSurfacePos, &mWaterSurfaceRot, nullptr);
    MR::setEffectName(this, "SwimSpinLight", "水泳スピン");
    MR::setEffectName(this, "DiveBubble", "水面Ｚ沈降");
    MR::addEffect(this, "WaterColumnS");
    MR::setEffectName(this, "WaterColumnS", "水面ジャンプ水柱");
    MR::setEffectName(this, "SurfaceWave", "水面泳ぎ");
    MR::setEffectHostSRT(this, "水面泳ぎ", &mWaterSurfacePos, &mWaterSurfaceRot, nullptr);
    MR::setEffectName(this, "BodyBubble", "水中身体泡");
    MR::setEffectName(this, "MouthBubble", "水中口泡");
    MR::setEffectName(this, "SeaSmoke", "水底接触");
    MR::setEffectName(this, "Metamorphosis", "変身");
    MR::setEffectName(this, "MetamorphosisEnd", "変身解除");
    MR::setEffectName(this, "BeeFlowerPollen", "ハチ花くっつき");
    MR::setEffectHostSRT(this, "ハチ花くっつき", nullptr, nullptr, nullptr);
    MR::setEffectName(this, "SkateL", "スケート左");
    MR::setEffectName(this, "SkateR", "スケート右");
    MR::setEffectName(this, "IceJumpLand", "スケート着地");

    MR::setEffectHostSRT(this, "天井ヒット", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "壁ヒット", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "パンチヒット", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "FlowerSpin", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "水波紋", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "結界ヒット", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "水壁ヒット", nullptr, nullptr, nullptr);

    MR::setEffectName(this, "Blur", "ワープポッドブラー");
    MR::setEffectName(this, "SpinRing", "スピンリング");
    MR::setEffectName(this, "Vanish", "ブラックホール消滅");

    {
        const char* groupName = "雪煙レベル";
        mEffectKeeper->registerEffect("CrestGetFlyC", mFootRMtx.toMtxPtr(), groupName);
    }

    const Vec3f* scale = &mScale;
    const Vec3f* rot = &mRotation;
    const Vec3f* pos = &mPosition;

    {
        const char* groupName = "スイングフライ";
        mEffectKeeper->registerEffect("CommonPhotonDustCircle", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "宇宙遊泳";
        mEffectKeeper->registerEffect("MarioSpaceDust", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "ジャンプフェアリー";
        mEffectKeeper->registerEffect("CrestGetFlyC", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "Bダッシュ";
        mEffectKeeper->registerEffect("MiniPandaSpinLoopLeg", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "インダイレクトテスト";
        mEffectKeeper->registerEffect("IndTest", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "ラケット消去";
        mEffectKeeper->registerEffect("TestMarioBaobabooPowerDown", mHipMtx.toMtxPtr(), groupName);
    }

    {
        const char* groupName = "チャージ完了";
        mEffectKeeper->registerEffect("TornadoChargeEnd", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "チャージ中";
        mEffectKeeper->registerEffect("TornadoCharge", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "チャージ切れ";
        mEffectKeeper->registerEffect("AppleHit", pos, rot, scale, groupName);
    }

    MR::setEffectName(this, "LandRubber", "特殊着地");
    MR::setEffectName(this, "DieDarkMatter", "ダークマター死亡");
    MR::setEffectName(this, "HopperMarioHipDropBlur", "ホッパー尻落");
    MR::setEffectName(this, "HopperLuigiHipDropBlur", "ホッパー尻落ルイージ");
    MR::setEffectName(this, "HipDropBlur", "尻落");
    MR::setEffectName(this, "HipDropBlurLuigi", "尻落ルイージ");
    MR::setEffectName(this, "GetAgain", "アイテム再ゲット");

    {
        const char* groupName = "パンチブラー左";
        mEffectKeeper->registerEffect("MarioPunchLBlur", pos, rot, scale, groupName);
    }

    MR::getEffect(this, "パンチブラー左")->forceFollowOn();
    {
        const char* groupName = "パンチブラー右";
        mEffectKeeper->registerEffect("MarioPunchRBlur", pos, rot, scale, groupName);
    }

    MR::getEffect(this, "パンチブラー右")->forceFollowOn();

    ModelObj* obj = new ModelObj("WaterColumn", "WaterColumn", nullptr, -2, -2, -2, false);
    mWaterColumn = obj;
    obj->kill();
}

void MarioActor::emitEffectWaterColumn(const Vec3f& rDir, const Vec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    mWaterColumn->appear();
    MR::startBck(mWaterColumn, "WaterColumn");
    MR::startBrk(mWaterColumn, "WaterColumn");

    Vec3f rot;
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, Vec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    mWaterColumn->mPosition = rTrans;
    mWaterColumn->mRotation = rot;
    mWaterColumn->calcAnim();

    MR::emitEffect(mWaterColumn, "WaterColumn");
    Mario* player = mMario;
    player->pushTask(&Mario::taskOnEffectCheck, 2);
}

// A common effect's name starts with 共, a material effect's with 属 (3 bytes each in UTF-8).
bool MarioActor::isCommonEffect(const char* pName) const {
    return MR::isHeadString(pName, "共");
}

bool MarioActor::isMaterialEffect(const char* pName) const {
    return MR::isHeadString(pName, "属");
}

MultiEmitter* MarioActor::playEffect(const char* pName) {
    s32 isBlocked = 0;
    if (mHidden || mMorphHidden) {
        isBlocked = 1;
    }

    if (isBlocked) {
        return nullptr;
    }

    if (isCommonEffect(pName)) {
        return playCommonEffect(pName);
    }

    if (isMaterialEffect(pName)) {
        return playMaterialEffect(pName);
    }

    return MR::emitEffect(this, pName);
}

void MarioActor::playEffectTrans(const char* pName, const Vec3f& rTrans) {
    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->setGlobalTranslation(rTrans, -1);
    }
}

void MarioActor::playEffectRT(const char* pName, const Vec3f& rDir, const Vec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    Vec3f rot;
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, Vec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->forceFollowOff();
        emitter->setGlobalTranslation(rTrans, -1);
        emitter->setGlobalRotationDegree(rot, -1);
    }
}

void MarioActor::playEffectRTZ(const char* pName, const Vec3f& rDir, const Vec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    Vec3f rot;
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, Vec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->forceFollowOff();
        emitter->setGlobalTranslation(rTrans, -1);
        emitter->setGlobalRotationDegree(rot, -1);
    }
}

void MarioActor::playEffectRTW(const char* pName, const Vec3f& rDir, const Vec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    Vec3f rot;
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, Vec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    mWaterSurfaceRot = rot;
    mWaterSurfacePos = rTrans;
    playEffect(pName);
}

void MarioActor::playEffectSRT(const char* pName, f32 scale, const Vec3f& rDir, const Vec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    Vec3f rot;
    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, Vec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->setGlobalScale(scale, -1);
    }

    if (emitter != nullptr) {
        emitter->setGlobalTranslation(rTrans, -1);
    }

    if (emitter != nullptr) {
        emitter->setGlobalRotationDegree(rot, -1);
    }
}

void MarioActor::stopCommonEffect(const char* pName) {
    const u32 hash = MR::getHashCode(pName);
    for (u32 i = 0; i < mCommonEffectCount; i++) {
        SmokeEffectEntry* entry = &cSmokeTable[i];
        if (entry->mHash == hash) {
            if (entry->mEmitter != nullptr) {
                entry->mEmitter->deleteEmitter();
                entry->mEmitter = nullptr;
            }
        }
    }
}

void MarioActor::stopEffect(const char* pName) {
    if (isCommonEffect(pName)) {
        stopCommonEffect(pName);
        return;
    }

    if (isMaterialEffect(pName)) {
        stopMaterialEffect(pName);
        return;
    }

    MR::deleteEffect(this, pName);
}

void MarioActor::stopEffectForce(const char* pName) {
    if (isCommonEffect(pName)) {
        stopCommonEffect(pName);
        return;
    }

    if (isMaterialEffect(pName)) {
        stopMaterialEffect(pName);
        return;
    }

    MR::forceDeleteEffect(this, pName);
}

void MarioActor::updateEffect() {
    s32 effectA = 0;
    s32 effectB = 0;
    s32 effectC = 0;

    if (mMario->mMovementStates.turnBrake) {
        effectA = 1;
    }

    if (mMario->mMovementStates.tornado && mMario->mMovementStates.onGround) {
        effectA = 1;
    }

    if (mMario->mMovementStates.onGround && mMario->mSpinInertiaBlend > 0.1f) {
        effectA = 1;
    }

    if (mMario->isStatusActive(MarioStatus_Flip)) {
        effectA = 1;
    }

    if (getMovementStates().onIce || getMovementStates().onSkatableFloor) {
        if (mMario->mWalkSpeed > 0.1f) {
            effectA = 1;
        }

        if (effectA != 0) {
            effectB = 1;
            effectA = 0;
        }
    }

    if (mRemoteDemo || mPlayerMode == 1 || mMario->isStatusActive(MarioStatus_Swim) || mMario->isStatusActive(MarioStatus_FpView) || mInRush) {
        effectA = 0;
        effectB = 0;
    }

    if (effectA) {
        playEffect("共通砂煙レベル");
    } else if (!effectA) {
        if (mEffectFlags.mSmoke != 0) {
            stopEffect("共通砂煙レベル");
        }
    }

    if (effectB && mEffectFlags.mSnow == 0) {
        playEffect("雪煙レベル");
    } else if (!effectB && mEffectFlags.mSnow != 0) {
        stopEffect("雪煙レベル");
    }

    mEffectFlags.mSmoke = effectA;
    mEffectFlags.mSnow = effectB;

    effectA = 0;
    if (getMovementStates().slopeSlide && !getMovementStates().baseRotated && getMovementStates().onGround) {
        effectA = 1;
    }

    if (effectA) {
        playEffect("共通スリップ坂");
    } else if (!effectA) {
        if (mEffectFlags.mSlopeSlip != 0) {
            stopEffect("共通スリップ坂");
        }
    }

    mEffectFlags.mSlopeSlip = effectA;

    if ((mHealth <= 1 || mWaterLife <= 1) && !MR::isDemoActive() && !MR::isPowerStarGetDemoActive() && !MR::isGalaxyDarkCometAppearInCurrentStage() &&
        MR::isPermitSE() && isEnableNerveChange() && ::cLowHealthWarning) {
        playSound("ライフ警告", -1);
    }

    s32 nearZero = 0;
    if (MR::isNearZero(mGravityVec)) {
        nearZero = 1;
    }

    if (!nearZero && mEffectFlags.mIsStationary) {
        playEffect("宇宙遊泳");
    } else if (nearZero && !mEffectFlags.mIsStationary) {
        stopEffect("宇宙遊泳");
    }

    mEffectFlags.mIsStationary = nearZero;

    if (!isJumping() && mInRush == 0) {
        MR::deleteEffect(this, "ジャンプフェアリー");
    }

    mMarioEffect->doCubeEffect();

    effectC = 0;
    if (getMovementStates().jumping) {
        if (mPlayerMode == 4 && mMario->checkLvlA() && mMario->mAirWalkTimer != 0 && ((mFrameCount & 3) == 0)) {
            if (mMario->mVerticalSpeed < 300.0f) {
                effectC = 1;
            } else if (mMario->mSwim->mDeepWater != 0) {
                f32 swimSpeed = mMario->mSwim->mSurfaceDepth;
                if (swimSpeed < 300.0f) {
                    effectC = 1;
                }
            }
        }
    }

    if (effectC) {
        playEffectRT("属性ハチ風", mMario->mGroundNorm, mMario->mShadowPos);
    } else {
        if (mEffectFlags.mBeeWind) {
            stopEffect("属性ハチ風");
        }
    }

    mEffectFlags.mBeeWind = effectC;

    u32 i = 0;
    while (i < mSmokeEntryCount) {
        SmokeEffectEntry* entry = mSmokeEntries[i];
        if (entry->mTimer != 0) {
            entry->mTimer--;
            if (entry->mTimer == 0) {
                stopEffect(entry->mName);
            }
        }

        i++;
    }
}

MarioEffect::MarioEffect(MarioActor* pActor) : MarioModule(pActor), mFollowCount(0) {
    mCubeArg0 = -1;
    mCubeArg1 = -1;
    mCubeColorOn = 0;
    mCubePrmColor.set(0, 0, 0, 0);
    mCubeEnvColor.set(0, 0, 0, 0);
    NebulaMtxIdentity(mCommonEffectMtx.toMtxPtr());
}

void MarioEffect::playSwingEffect() {
    if (mCubeArg0 == 1) {
        switch (mCubeArg1) {
        default:
            break;
        case 0:
            playEffectTrans("FlowerSpin", getShadowPos());
            break;
        }
    }

    Mario* mario = getPlayer();
    playEffectRT("属性スピン", mario->mGroundNorm, getTrans());
}

void MarioEffect::doCubeEffect() {
    s32 unsetArg = -1;
    mCubeArg1 = unsetArg;
    mCubeArg0 = unsetArg;

    AreaObj* area = MR::getAreaObj("EffectCylinder", mActor->mPosition);
    if (area != nullptr) {
        s32 cubeArg = MR::getAreaObjArg(area, 0);
        mCubeArg1 = MR::getAreaObjArg(area, 1);
        mCubeArg0 = cubeArg;
    } else {
    }

    AreaObj* colorArea = MR::getAreaObj("SmokeEffectColorArea", mActor->mPosition);
    if (colorArea != nullptr) {
        const s32 prmR = MR::getAreaObjArg(colorArea, 0);
        const s32 prmG = MR::getAreaObjArg(colorArea, 1);
        const s32 prmB = MR::getAreaObjArg(colorArea, 2);
        mCubePrmColor.set(prmR, prmG, prmB, 0);

        const s32 envR = MR::getAreaObjArg(colorArea, 3);
        const s32 envG = MR::getAreaObjArg(colorArea, 4);
        const s32 envB = MR::getAreaObjArg(colorArea, 5);
        mCubeEnvColor.set(envR, envG, envB, 0);
        mCubeColorOn = 1;
    } else {
        mCubeColorOn = 0;
    }

    if (getPlayer()->mActionStates.swingEffectPending) {
        playSwingEffect();
        getPlayer()->mActionStates.swingEffectPending = 0;
    }
}

bool Mario::taskOnEffectCheck(u32 flags) {
    if (flags & 0x2) {
        return mActor->checkEffectWaterColumn();
    }

    return true;
}

bool MarioActor::checkEffectWaterColumn() {
    if (MR::isBckStopped(mWaterColumn)) {
        mWaterColumn->kill();
        return false;
    }

    return true;
}

void MarioActor::stopSpinTicoEffect(bool force) {
    mSpinTicoTimer = 0;
    mSpinStarTimer = 0;
    mSpinStarEndTimer = 0;
    mSpinIntervalTimer = 0;

    if (force) {
        stopEffectForce("スピンリング");
    } else {
        stopEffect("スピンリング");
    }
}

MarioEffect::~MarioEffect() {
}

void MultiEmitter::setGlobalScale(f32 scale, s32 idx) {
    Vec3f scaleVec(scale, scale, scale);
    setGlobalScale(scaleVec, idx);
}
