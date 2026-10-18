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
#include <JSystem/JParticle/JPAEmitter.hpp>
#include <JSystem/JParticle/JPAMath.hpp>
#include <cstdio>
#include <cstring>

struct MaterialEffectEntry {
    union FlagWord {
        u32 mWord;
        struct {
            u8 mByte0;
            u8 mByte1;
            u8 mByte2;
            u8 mByte3;
        };
    };

    /* 0x00 */ const char* mName;
    /* 0x04 */ FlagWord mFlag;
    /* 0x08 */ const char* mEffects[7];
    /* 0x24 */ const char* mCurrent;
    /* 0x28 */ MultiEmitter* mEmitter;
};

struct SmokeEffectEntry {
    union FlagWord {
        u32 mWord;
        struct {
            u8 mByte0;
            u8 mByte1;
            u8 mByte2;
            u8 mByte3;
        };
    };

    /* 0x00 */ const char* mName;
    /* 0x04 */ FlagWord mType;
    /* 0x08 */ f32 mScale;
    /* 0x0C */ f32 mRate;
    /* 0x10 */ FlagWord mFlags;
    /* 0x14 */ u16 mInterval;
    /* 0x16 */ u16 mTimer;
    /* 0x18 */ u32 mHash;
    /* 0x1C */ MultiEmitter* mEmitter;
    /* 0x20 */ u8 mMaterial;
    /* 0x21 */ u8 _21[3];
};

MaterialEffectEntry cMaterialEffectTable[] = {
    {"\221\256\220\253\220\113\203\150\203\215\203\142\203\166" /* 属性尻ドロップ */,
     0x00000000,
     {"HipDropSmoke", "HipDropWater", "HipDropFlower", "HipDropSand", "HipDropSnow", "MudCircle", "HoneyCircle"},
     nullptr,
     nullptr},
    {"\221\256\220\253\203\156\203\140\225\227" /* 属性ハチ風 */, 0x03000000, {"Hovering", "HoveringWater", "Hovering", "Hovering", "Hovering", "Hovering", "Hovering"}, nullptr, nullptr},
    {"\221\256\220\253\203\130\203\163\203\223" /* 属性スピン */, 0x00000000, {nullptr, "WaterSpin", "FlowerSpin", "SandSpin", "SnowSpin", "MudCircle", "HoneyCircle"}, nullptr, nullptr},
    {"\221\256\220\253\203\130\203\145\201\133\203\127\203\103\203\223" /* 属性ステージイン */,
     0x00000000,
     {"StageStartGroundSmoke", nullptr, nullptr, "StageStartGroundSand", nullptr, nullptr, nullptr},
     nullptr,
     nullptr},
    {nullptr, 0, {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, nullptr, nullptr}};

namespace {

    void addMarioEffectAtJoint(LiveActor* pActor, const char* pEffectName, const char* pJointName, const char* pGroupName) NO_INLINE {
        MtxPtr mtx = pJointName != nullptr ? MR::getJointMtx(pActor, pJointName) : pActor->getBaseMtx();
        pActor->mEffectKeeper->registerEffect(pEffectName, mtx, &pActor->mScale, pGroupName, pJointName);
    }

};  // namespace

void MarioEffect::execute(JPABaseEmitter* pEmitter) {
    s32 idx = 0;
    for (s32 i = 0; i < 256; i++) {
        if (_54[i]._68 == pEmitter) {
            break;
        }

        idx++;
    }

    updateFollowMtx(&_54[idx]);
}

JPABaseEmitter* MarioEffect::addRequest(const char* pEffectName, MtxPtr pMtx) {
    u32 requestIndex = 0;
    for (; requestIndex < 256; requestIndex++) {
        if (_54[requestIndex]._68 == nullptr) {
            break;
        }
    }

    if (requestIndex == 256) {
        requestIndex = 0;
        for (s32 i = 1; i < 256; i++) {
            if (_54[i]._0 > _54[requestIndex]._0) {
                requestIndex = i;
            }
        }
    }

    MovingFollowMtx* target = &_54[requestIndex];

    MultiEmitter* emitter = MR::emitEffectWithEmitterCallBack(mActor, pEffectName, this);
    if (emitter->getParticleEmitter(0) == nullptr) {
        return nullptr;
    }

    JPABaseEmitter* baseEmitter = emitter->getParticleEmitter(0)->mEmitter;
    target->_68 = baseEmitter;

    for (s32 i = 0; i < 256; i++) {
        if (requestIndex != i && _54[i]._68 == target->_68) {
            _54[i]._68 = nullptr;
        }
    }

    target->_0 = 0;
    target->_64 = pMtx;
    return target->_68;
}

void MarioEffect::updateFollowMtx(MovingFollowMtx* pFollow) {
    if (pFollow->_68->isEnableDeleteEmitter()) {
        pFollow->_68 = nullptr;
        return;
    }

    if (pFollow->_0 == 0) {
        Mtx temp;
        PSMTXCopy(_24, temp);

        const TVec3f& rTrans = getTrans();
        MR::setMtxTrans(temp, rTrans.x, rTrans.y, rTrans.z);

        if (pFollow->_64 == nullptr) {
            pFollow->_64 = reinterpret_cast< MtxPtr >(getPlayer()->getMoveBaseMtx());
            if (pFollow->_64 == nullptr) {
                return;
            }
        }

        Mtx inv;
        PSMTXInverse(pFollow->_64, inv);
        MR::multMtx(pFollow->_4, temp, inv);
        PSMTXCopy(temp, pFollow->_34);
    } else {
        MR::multMtx(pFollow->_34, pFollow->_4, pFollow->_64);
    }

    pFollow->_68->setGlobalRTMatrix(pFollow->_34);

    pFollow->_0++;
    if (pFollow->_0 == 2) {
        pFollow->_68->playCalcEmitter();
        pFollow->_68->becomeInvalidEmitter();
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

    _BA4 = new HashSortTable(entryCount);
    entry = cMaterialEffectTable;
    for (int i = 0; i < entryCount; i++) {
        _BA4->add(cMaterialEffectTable[i].mName, reinterpret_cast< HashSortValue >(&cMaterialEffectTable[i]), false);
    }

    _BA4->sort();
}

s32 MarioActor::getFloorMaterialIndex(u32 flags) const {
    s32 materialIndex = 0;

    if (mMarioEffect->_10 == 0) {
        return 2;
    }

    if (_3A8 != 0) {
        return 1;
    }

    s32 floor = mMario->_960;
    if ((floor == 0x0D || floor == 0x1E) && !mMario->isCurrentFloorSand()) {
        floor = mMario->_962;
    }

    if (mMario->isStatusActive(MarioStatus_Wall)) {
        if (mMario->mMovementStates._8) {
            floor = mMario->_964[0];
        } else if (mMario->mMovementStates._19) {
            floor = mMario->_964[1];
        } else if (mMario->mMovementStates._1A) {
            floor = mMario->_964[2];
        }
    }

    if (mMario->mDrawStates.mIsUnderwater) {
        floor = 0x17;
    }

    if (mMario->mDrawStates._13) {
        floor = 0x17;
    }

    if (flags & 0x2) {
        if (mMario->mDrawStates._1D) {
            floor = 0x17;
        }
    }

    if (mMario->_2D0 != 0.0f) {
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
        if (strcmp(MR::getSoundCodeString(mMario->_45C), "Honey") == 0) {
            materialIndex = 6;
        }

        break;
    case 0x1B:
    case 0x1C:
        if (strcmp(MR::getSoundCodeString(mMario->_45C), "Sand") == 0) {
            materialIndex = 3;
        }

        break;
    default:
        break;
    }

    if (mMario->mDrawStates.mIsUnderwater || mMario->mDrawStates._13) {
        materialIndex = 1;
    }

    if (!mMario->mMovementStates._1) {
        if (mMario->mSwim->_1B2 != 0) {
            if (mMario->mSwim->_1B8 < 400.0f) {
                materialIndex = 1;
            }
        }
    }

    return materialIndex;
}

MultiEmitter* MarioActor::playMaterialEffect(const char* pName) {
    MaterialEffectEntry* entry = nullptr;
    _BA4->search(pName, reinterpret_cast< HashSortValue* >(&entry));

    const s32 materialIndex = getFloorMaterialIndex(entry->mFlag.mByte0);
    if (materialIndex == -1) {
        return nullptr;
    }

    const u32 materialOffset = static_cast< u32 >(materialIndex);
    const char* effectName = entry->mEffects[materialOffset];
    if (effectName == nullptr) {
        return nullptr;
    }

    if (entry->mFlag.mByte0 == 1) {
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
        if (marioEffect->_18) {
            emitter->setGlobalPrmColor(marioEffect->_1C.r, marioEffect->_1C.g, marioEffect->_1C.b, -1);
            emitter->setGlobalEnvColor(marioEffect->_20.r, marioEffect->_20.g, marioEffect->_20.b, -1);
        } else {
            emitter->setGlobalPrmColor(0xFF, 0xFF, 0xFF, -1);
            emitter->setGlobalEnvColor(0xFF, 0xFF, 0xFF, -1);
        }
    }

    return emitter;
}

void MarioActor::stopMaterialEffect(const char* pName) {
    MaterialEffectEntry* entry = nullptr;
    _BA4->search(pName, reinterpret_cast< HashSortValue* >(&entry));

    if (entry->mCurrent != nullptr) {
        MR::deleteEffect(this, entry->mCurrent);
        entry->mCurrent = nullptr;
        entry->mEmitter = nullptr;
    }
}

SmokeEffectEntry cSmokeTable[] = {{"\213\244\222\312\222\205\222\156\225\201\222\312" /* 共通着地普通 */, 0x08000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\222\205\222\156\221\345" /* 共通着地大 */, 0x03000000, 1.5f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\225\307\203\127\203\203\203\223\203\166" /* 共通壁ジャンプ */, 0x03000000, 1.0f, 1.0f, 0x00000300, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\215\273\211\214\203\214\203\170\203\213" /* 共通砂煙レベル */, 0x07000000, 0.65f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\225\307\216\350\216\103\202\350" /* 共通壁手擦り */, 0x01000000, 0.35f, 1.0f, 0x02010000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\225\307\217\343\217\270" /* 共通壁上昇 */, 0x01000000, 1.0f, 1.0f, 0x00000200, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\130\203\212\203\142\203\166\215\342" /* 共通スリップ坂 */, 0x02000000, 0.35f, 1.0f, 0x00010000, 2, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\130\203\212\203\142\203\166\213\363\223\135" /* 共通スリップ空転 */, 0x00000000, 1.0f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\130\203\212\203\142\203\166\215\342\220\247\223\256" /* 共通スリップ坂制動 */, 0x02000000, 0.5f, 1.0f, 0x00000000, 2, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\225\307\203\161\203\142\203\147\222\205\222\156" /* 共通壁ヒット着地 */, 0x03000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\137\203\201\201\133\203\127\222\205\222\156" /* 共通ダメージ着地 */, 0x03000000, 1.0f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\222\156\217\343\203\130\203\163\203\223" /* 共通地上スピン */, 0x03000000, 0.9f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\165\203\214\201\133\203\114" /* 共通ブレーキ */, 0x02000000, 0.65f, 1.0f, 0x00010000, 15, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\156\203\103\203\127\203\203\203\223\203\166" /* 共通ハイジャンプ */, 0x02000000, 0.65f, 1.0f, 0x00010000, 6, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\203\165\203\211\203\142\203\116\203\172\201\133\203\213" /* 共通ブラックホール */, 0x01000000, 0.65f, 1.0f, 0x03030000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\210\370\202\253\226\337\202\265\222\205\222\156" /* 共通引き戻し着地 */, 0x03000000, 1.2f, 1.0f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\220\205\222\352\220\332\220\107" /* 共通水底接触 */, 0x01000000, 6.0f, 0.5f, 0x00000400, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\222\265\226\364" /* 共通跳躍 */, 0x08000000, 1.0f, 1.0f, 0x00000C00, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\202\320\202\261\202\244\202\253\211\137" /* 共通ひこうき雲 */, 0x03000000, 0.3f, 0.2f, 0x00010000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {"\213\244\222\312\202\320\202\261\202\244\202\253\203\165\201\133\203\130\203\147" /* 共通ひこうきブースト */, 0x00000000, 2.0f, 1.0f, 0x00000000, 0, 0, 0, nullptr, 0, {0, 0, 0}},
                                  {nullptr, 0, 0.0f, 0.0f, 0, 0, 0, 0, nullptr, 0, {0, 0, 0}}};

void MarioActor::initCommonEffect() {
    _B9C = 0;
    _B9E = 0;
    _BA0 = new SmokeEffectEntry*[8];

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

        switch (entry->mType.mByte0) {
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

        char name[0x80];
        for (u32 materialIndex = 0; materialIndex < 7; materialIndex++) {
            sprintf(name, "%s", entry->mName);

            const u8 flag2 = entry->mFlags.mByte2;
            if ((flag2 & 0x8) && materialIndex != 1) {
                continue;
            }

            const char* const* effectList;
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
                const u8 flag0 = entry->mFlags.mByte0;
                switch (flag0) {
                case 0: {
                    const char* effectName = effectList[variant];
                    const char* hostName = name + materialIndex;
                    mEffectKeeper->registerEffectWithoutSRT(effectName, hostName);
                    const TVec3f* rot = &mRotation;
                    const TVec3f* pos = &mPosition;
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

            if ((entry->mFlags.mByte1 & 0x1) != 0) {
                MR::getEffect(this, name + materialIndex)->forceFollowOn();
            }

            if ((entry->mFlags.mByte1 & 0x2) != 0) {
                MR::getEffect(this, name + materialIndex)->forceScaleOn();
            }
        }

        entry->mTimer = 0;
        switch (entry->mType.mByte0) {
        case 2:
        case 5: {
            _BA0[_B9E] = entry;
            _B9E++;
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
        _B9C++;
    }
}

MultiEmitter* MarioActor::playCommonEffect(const char* pName) {
    if (mPlayerMode == 3) {
        return nullptr;
    }

    const u32 hash = MR::getHashCode(pName);
    for (u32 i = 0; i < _B9C; i++) {
        SmokeEffectEntry* entry = &cSmokeTable[i];
        if (entry->mHash == hash) {
            MultiEmitter* emitter = nullptr;
            MarioEffect* marioEffect;
            s32 materialIndex = getFloorMaterialIndex(0);

            if (materialIndex == -1) {
                return nullptr;
            }

            if ((entry->mFlags.mByte2 & 0x8) && materialIndex != 1) {
                return nullptr;
            }

            switch (entry->mType.mByte0) {
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
                    if (entry->mType.mByte0 >= 6) {
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
                    JPABaseEmitter* baseEmitter = mMarioEffect->addRequest(pName + materialIndex, tri->getBaseMtx()->toMtxPtr());

                    if (baseEmitter != nullptr) {
                        baseEmitter->setGlobalScale(TVec3f(entry->mScale, entry->mScale, entry->mScale));
                    }
                } else {
                    if (needsSRT) {
                        const u8 hostFlag = entry->mFlags.mByte1;
                        const TVec3f* hostPos = nullptr;
                        const TVec3f* hostRot = nullptr;
                        const TVec3f* hostScale = nullptr;

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

            if (!(entry->mFlags.mByte1 & 0x2)) {
                if (emitter != nullptr) {
                    emitter->setGlobalScale(entry->mScale, -1);
                }
            }

            if (emitter != nullptr) {
                emitter->setRate(entry->mRate, -1);
            }

            if (entry->mFlags.mByte2 & 0x1) {
                TVec3f dir(mMario->getWallNorm());
                TPos3f mtx;
                TVec3f rot;
                MR::makeMtxUpNoSupportPos(&mtx, dir, TVec3f(0.0f, 0.0f, 0.0f));
                MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

                if (emitter != nullptr) {
                    emitter->setGlobalRotationDegree(rot, -1);
                }

                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(mPosition, -1);
                }
            } else {
                TVec3f dir(mMario->_368);
                TPos3f mtx;
                TVec3f rot;
                MR::makeMtxUpNoSupportPos(&mtx, dir, TVec3f(0.0f, 0.0f, 0.0f));
                MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);
                PSMTXCopy(mtx.toMtxPtr(), mMarioEffect->_24.toMtxPtr());

                if (emitter != nullptr) {
                    emitter->setGlobalRotationDegree(rot, -1);
                }

                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(mPosition, -1);
                }
            }

            if (entry->mFlags.mByte2 & 0x2) {
                TVec3f trans(mMario->_4E8);
                if (emitter != nullptr) {
                    emitter->setGlobalTranslation(trans, -1);
                }
            }

            if (entry->mInterval != 0) {
                entry->mTimer = entry->mInterval;
            }

            switch (entry->mType.mByte0) {
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
                if (marioEffect->_18) {
                    emitter->setGlobalPrmColor(marioEffect->_1C.r, marioEffect->_1C.g, marioEffect->_1C.b, -1);
                    emitter->setGlobalEnvColor(marioEffect->_20.r, marioEffect->_20.g, marioEffect->_20.b, -1);
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
    _B98 = 0;
    initEffectKeeper(0xB8, "Mario", true);
    initCommonEffect();
    initMaterialEffect();

    MR::setEffectName(this, "SlipUp", "\203\130\203\212\203\142\203\166\203\101\203\142\203\166" /* スリップアップ */);
    MR::setEffectName(this, "WallSpark", "\225\307\203\130\203\160\201\133\203\116" /* 壁スパーク */);
    MR::setEffectName(this, "Damage", "\203\137\203\201\201\133\203\127" /* ダメージ */);
    MR::setEffectName(this, "StepOn", "\202\323\202\335\202\302\202\324\202\265" /* ふみつぶし */);
    MR::setEffectName(this, "SuperSpinDriverEnd", "\203\130\201\133\203\160\201\133\203\130\203\163\203\223\203\150\203\211\203\103\203\157\217\111\227\271" /* スーパースピンドライバ終了 */);
    MR::setEffectName(this, "BringBubble", "\210\370\202\253\226\337\202\265\226\101" /* 引き戻し泡 */);
    MR::setEffectName(this, "BringBubbleBreak", "\210\370\202\253\226\337\202\265\226\101\224\152\227\364" /* 引き戻し泡破裂 */);
    MR::setEffectName(this, "WaterSplashRight", "\220\205\202\315\202\313\211\105" /* 水はね右 */);
    MR::setEffectName(this, "WaterSplashLeft", "\220\205\202\315\202\313\215\266" /* 水はね左 */);
    MR::setEffectName(this, "WaterSplashSRight", "\220\205\202\315\202\313\211\105\216\343" /* 水はね右弱 */);
    MR::setEffectName(this, "WaterSplashSLeft", "\220\205\202\315\202\313\215\266\216\343" /* 水はね左弱 */);
    MR::setEffectName(this, "SpinLightBurst", "\203\130\203\163\203\223\203\211\203\103\203\147\217\301\213\216" /* スピンライト消去 */);
    MR::setEffectName(this, "ElectricDamage", "\203\162\203\212\203\162\203\212" /* ビリビリ */);
    MR::setEffectName(this, "FireDamageSmoke", "\211\212\203\137\203\201\201\133\203\127\211\214" /* 炎ダメージ煙 */);
    MR::setEffectName(this, "BlueFireDamageSmoke", "\211\212\203\137\203\201\201\133\203\127\220\302\211\214" /* 炎ダメージ青煙 */);
    MR::setEffectName(this, "SpinLight", "\203\130\203\163\203\223\203\211\203\103\203\147" /* スピンライト */);
    MR::setEffectName(this, "IceMarioSpinLight", "\203\101\203\103\203\130\203\130\203\163\203\223" /* アイススピン */);
    MR::setEffectName(this, "FireMarioSpinLight", "\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */);
    MR::setEffectName(this, "BeeMarioSpinLight", "\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */);
    MR::setEffectName(this, "BeeLuigiSpinLight", "\203\156\203\140\203\213\203\103\201\133\203\127\203\130\203\163\203\223" /* ハチルイージスピン */);
    MR::setEffectName(this, "Sweat", "\202\242\202\242\212\276" /* いい汗 */);
    MR::setEffectName(this, "CarryStartShort", "\202\320\202\353\202\242\203\116\203\103\203\142\203\116" /* ひろいクイック */);
    MR::setEffectName(this, "AirRecover", "\216\137\221\146\211\361\225\234" /* 酸素回復 */);
    MR::setEffectName(this, "Invincible", "\226\263\223\107\222\206" /* 無敵中 */);
    MR::setEffectHostSRT(this, "\226\263\223\107\222\206" /* 無敵中 */, &mPosition, nullptr, nullptr);
    MR::setEffectName(this, "Ice", "\203\101\203\103\203\130\222\206" /* アイス中 */);
    MR::setEffectHostSRT(this, "\203\101\203\103\203\130\222\206" /* アイス中 */, &mPosition, nullptr, nullptr);
    MR::setEffectName(this, "FireBallThrow", "\203\164\203\100\203\103\203\101\203\173\201\133\203\213\223\212\202\260" /* ファイアボール投げ */);
    MR::setEffectName(this, "Throw", "\202\261\202\244\202\347\223\212\202\260" /* こうら投げ */);
    MR::setEffectName(this, "DieBuryBody", "\215\273\226\204\202\334\202\350\221\314" /* 砂埋まり体 */);
    MR::setEffectName(this, "DieBuryHand", "\215\273\226\204\202\334\202\350\216\350" /* 砂埋まり手 */);
    MR::setEffectName(this, "DieDeathMudBody", "\223\104\226\204\202\334\202\350\221\314" /* 泥埋まり体 */);
    MR::setEffectName(this, "DieDeathMudHand", "\223\104\226\204\202\334\202\350\216\350" /* 泥埋まり手 */);
    MR::setEffectName(this, "SpinLightConcentrate", "\203\130\203\163\203\223\213\226\211\302" /* スピン許可 */);
    MR::setEffectName(this, "FooMarioHandGlowL", "\203\164\201\133\203\175\203\212\203\111\203\117\203\215\201\133\215\266" /* フーマリオグロー左 */);
    MR::setEffectName(this, "FooMarioHandGlowR", "\203\164\201\133\203\175\203\212\203\111\203\117\203\215\201\133\211\105" /* フーマリオグロー右 */);
    MR::setEffectName(this, "FooMarioBrakeLampL", "\203\164\201\133\203\175\203\212\203\111\203\165\203\214\201\133\203\114\215\266" /* フーマリオブレーキ左 */);
    MR::setEffectName(this, "FooMarioBrakeLampR", "\203\164\201\133\203\175\203\212\203\111\203\165\203\214\201\133\203\114\211\105" /* フーマリオブレーキ右 */);
    MR::setEffectName(this, "FooMarioEndL", "\203\164\201\133\203\175\203\212\203\111\211\360\217\234\215\266" /* フーマリオ解除左 */);
    MR::setEffectName(this, "FooMarioEndR", "\203\164\201\133\203\175\203\212\203\111\211\360\217\234\211\105" /* フーマリオ解除右 */);
    MR::setEffectName(this, "WallHit", "\225\307\203\161\203\142\203\147" /* 壁ヒット */);
    MR::setEffectName(this, "BarrierBound", "\214\213\212\105\203\161\203\142\203\147" /* 結界ヒット */);
    MR::setEffectName(this, "WaterBound", "\220\205\225\307\203\161\203\142\203\147" /* 水壁ヒット */);
    MR::setEffectName(this, "IceWallJump", "\225\130\225\307\203\127\203\203\203\223\203\166" /* 氷壁ジャンプ */);
    MR::setEffectHostSRT(this, "\225\130\225\307\203\127\203\203\203\223\203\166" /* 氷壁ジャンプ */, nullptr, nullptr, nullptr);
    MR::setEffectName(this, "CeilingCrash", "\223\126\210\344\203\161\203\142\203\147" /* 天井ヒット */);
    MR::setEffectName(this, "Coin", "\203\122\203\103\203\223" /* コイン */);
    MR::setEffectName(this, "PunchHit", "\203\160\203\223\203\140\203\161\203\142\203\147" /* パンチヒット */);
    MR::setEffectName(this, "WaterRipple", "\220\205\224\147\226\344" /* 水波紋 */);
    MR::setEffectName(this, "SwimWaitRipple", "\220\205\226\312\203\105\203\107\203\103\203\147\224\147\226\344" /* 水面ウエイト波紋 */);
    MR::setEffectHostSRT(this, "\220\205\226\312\203\105\203\107\203\103\203\147\224\147\226\344" /* 水面ウエイト波紋 */, &_BAC, &_BB8, nullptr);
    MR::setEffectName(this, "SwimSpinLight", "\220\205\211\152\203\130\203\163\203\223" /* 水泳スピン */);
    MR::setEffectName(this, "DiveBubble", "\220\205\226\312\202\171\222\276\215\176" /* 水面Ｚ沈降 */);
    MR::addEffect(this, "WaterColumnS");
    MR::setEffectName(this, "WaterColumnS", "\220\205\226\312\203\127\203\203\203\223\203\166\220\205\222\214" /* 水面ジャンプ水柱 */);
    MR::setEffectName(this, "SurfaceWave", "\220\205\226\312\211\152\202\254" /* 水面泳ぎ */);
    MR::setEffectHostSRT(this, "\220\205\226\312\211\152\202\254" /* 水面泳ぎ */, &_BAC, &_BB8, nullptr);
    MR::setEffectName(this, "BodyBubble", "\220\205\222\206\220\147\221\314\226\101" /* 水中身体泡 */);
    MR::setEffectName(this, "MouthBubble", "\220\205\222\206\214\373\226\101" /* 水中口泡 */);
    MR::setEffectName(this, "SeaSmoke", "\220\205\222\352\220\332\220\107" /* 水底接触 */);
    MR::setEffectName(this, "Metamorphosis", "\225\317\220\147" /* 変身 */);
    MR::setEffectName(this, "MetamorphosisEnd", "\225\317\220\147\211\360\217\234" /* 変身解除 */);
    MR::setEffectName(this, "BeeFlowerPollen", "\203\156\203\140\211\324\202\255\202\301\202\302\202\253" /* ハチ花くっつき */);
    MR::setEffectHostSRT(this, "\203\156\203\140\211\324\202\255\202\301\202\302\202\253" /* ハチ花くっつき */, nullptr, nullptr, nullptr);
    MR::setEffectName(this, "SkateL", "\203\130\203\120\201\133\203\147\215\266" /* スケート左 */);
    MR::setEffectName(this, "SkateR", "\203\130\203\120\201\133\203\147\211\105" /* スケート右 */);
    MR::setEffectName(this, "IceJumpLand", "\203\130\203\120\201\133\203\147\222\205\222\156" /* スケート着地 */);

    MR::setEffectHostSRT(this, "\223\126\210\344\203\161\203\142\203\147" /* 天井ヒット */, nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "\225\307\203\161\203\142\203\147" /* 壁ヒット */, nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "\203\160\203\223\203\140\203\161\203\142\203\147" /* パンチヒット */, nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "FlowerSpin", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "\220\205\224\147\226\344" /* 水波紋 */, nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "\214\213\212\105\203\161\203\142\203\147" /* 結界ヒット */, nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "\220\205\225\307\203\161\203\142\203\147" /* 水壁ヒット */, nullptr, nullptr, nullptr);

    MR::setEffectName(this, "Blur", "\203\217\201\133\203\166\203\174\203\142\203\150\203\165\203\211\201\133" /* ワープポッドブラー */);
    MR::setEffectName(this, "SpinRing", "\203\130\203\163\203\223\203\212\203\223\203\117" /* スピンリング */);
    MR::setEffectName(this, "Vanish", "\203\165\203\211\203\142\203\116\203\172\201\133\203\213\217\301\226\305" /* ブラックホール消滅 */);

    {
        const char* groupName = "\220\341\211\214\203\214\203\170\203\213" /* 雪煙レベル */;
        mEffectKeeper->registerEffect("CrestGetFlyC", _D1C.toMtxPtr(), groupName, nullptr);
    }

    const TVec3f* scale = &mScale;
    const TVec3f* rot = &mRotation;
    const TVec3f* pos = &mPosition;

    {
        const char* groupName = "\203\130\203\103\203\223\203\117\203\164\203\211\203\103" /* スイングフライ */;
        mEffectKeeper->registerEffect("CommonPhotonDustCircle", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\211\106\222\210\227\126\211\152" /* 宇宙遊泳 */;
        mEffectKeeper->registerEffect("MarioSpaceDust", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\203\127\203\203\203\223\203\166\203\164\203\106\203\101\203\212\201\133" /* ジャンプフェアリー */;
        mEffectKeeper->registerEffect("CrestGetFlyC", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "B\203\137\203\142\203\126\203\205" /* Bダッシュ */;
        mEffectKeeper->registerEffect("MiniPandaSpinLoopLeg", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\203\103\203\223\203\137\203\103\203\214\203\116\203\147\203\145\203\130\203\147" /* インダイレクトテスト */;
        mEffectKeeper->registerEffect("IndTest", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\203\211\203\120\203\142\203\147\217\301\213\216" /* ラケット消去 */;
        mEffectKeeper->registerEffect("TestMarioBaobabooPowerDown", _DAC.toMtxPtr(), groupName, nullptr);
    }

    {
        const char* groupName = "\203\140\203\203\201\133\203\127\212\256\227\271" /* チャージ完了 */;
        mEffectKeeper->registerEffect("TornadoChargeEnd", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\203\140\203\203\201\133\203\127\222\206" /* チャージ中 */;
        mEffectKeeper->registerEffect("TornadoCharge", pos, rot, scale, groupName);
    }

    {
        const char* groupName = "\203\140\203\203\201\133\203\127\220\330\202\352" /* チャージ切れ */;
        mEffectKeeper->registerEffect("AppleHit", pos, rot, scale, groupName);
    }

    MR::setEffectName(this, "LandRubber", "\223\301\216\352\222\205\222\156" /* 特殊着地 */);
    MR::setEffectName(this, "DieDarkMatter", "\203\137\201\133\203\116\203\175\203\136\201\133\216\200\226\123" /* ダークマター死亡 */);
    MR::setEffectName(this, "HopperMarioHipDropBlur", "\203\172\203\142\203\160\201\133\220\113\227\216" /* ホッパー尻落 */);
    MR::setEffectName(this, "HopperLuigiHipDropBlur", "\203\172\203\142\203\160\201\133\220\113\227\216\203\213\203\103\201\133\203\127" /* ホッパー尻落ルイージ */);
    MR::setEffectName(this, "HipDropBlur", "\220\113\227\216" /* 尻落 */);
    MR::setEffectName(this, "HipDropBlurLuigi", "\220\113\227\216\203\213\203\103\201\133\203\127" /* 尻落ルイージ */);
    MR::setEffectName(this, "GetAgain", "\203\101\203\103\203\145\203\200\215\304\203\121\203\142\203\147" /* アイテム再ゲット */);

    {
        const char* groupName = "\203\160\203\223\203\140\203\165\203\211\201\133\215\266" /* パンチブラー左 */;
        mEffectKeeper->registerEffect("MarioPunchLBlur", pos, rot, scale, groupName);
    }

    MR::getEffect(this, "\203\160\203\223\203\140\203\165\203\211\201\133\215\266" /* パンチブラー左 */)->forceFollowOn();
    {
        const char* groupName = "\203\160\203\223\203\140\203\165\203\211\201\133\211\105" /* パンチブラー右 */;
        mEffectKeeper->registerEffect("MarioPunchRBlur", pos, rot, scale, groupName);
    }

    MR::getEffect(this, "\203\160\203\223\203\140\203\165\203\211\201\133\211\105" /* パンチブラー右 */)->forceFollowOn();

    mEffectKeeper->finalizeSort();

    ModelObj* obj = new ModelObj("WaterColumn", "WaterColumn", nullptr, -2, -2, -2, false);
    _BA8 = obj;
    obj->kill();
}

void MarioActor::emitEffectWaterColumn(const TVec3f& rDir, const TVec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    _BA8->appear();
    MR::startBck(_BA8, "WaterColumn");
    MR::startBrk(_BA8, "WaterColumn");

    TVec3f rot;
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, TVec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    _BA8->mPosition = rTrans;
    _BA8->mRotation = rot;
    _BA8->calcAnim();

    MR::emitEffect(_BA8, "WaterColumn");
    Mario* player = mMario;
    player->pushTask(&Mario::taskOnEffectCheck, 2);
}

bool MarioActor::isCommonEffect(const char* pName) const {
    if (static_cast< u8 >(pName[0]) == 0x8B) {
        if (static_cast< u8 >(pName[1]) == 0xA4) {
            return true;
        }
    }

    return false;
}

bool MarioActor::isMaterialEffect(const char* pName) const {
    if (static_cast< u8 >(pName[0]) == 0x91) {
        if (static_cast< u8 >(pName[1]) == 0xAE) {
            return true;
        }
    }

    return false;
}

MultiEmitter* MarioActor::playEffect(const char* pName) {
    s32 isBlocked = 0;
    if (_482 || _483) {
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

void MarioActor::playEffectTrans(const char* pName, const TVec3f& rTrans) {
    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->setGlobalTranslation(rTrans, -1);
    }
}

void MarioActor::playEffectRT(const char* pName, const TVec3f& rDir, const TVec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    TVec3f rot;
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, TVec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->forceFollowOff();
        emitter->setGlobalTranslation(rTrans, -1);
        emitter->setGlobalRotationDegree(rot, -1);
    }
}

void MarioActor::playEffectRTZ(const char* pName, const TVec3f& rDir, const TVec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    TVec3f rot;
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, TVec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    MultiEmitter* emitter = playEffect(pName);
    if (emitter != nullptr) {
        emitter->forceFollowOff();
        emitter->setGlobalTranslation(rTrans, -1);
        emitter->setGlobalRotationDegree(rot, -1);
    }
}

void MarioActor::playEffectRTW(const char* pName, const TVec3f& rDir, const TVec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    TVec3f rot;
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, TVec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);

    _BB8 = rot;
    _BAC = rTrans;
    playEffect(pName);
}

void MarioActor::playEffectSRT(const char* pName, f32 scale, const TVec3f& rDir, const TVec3f& rTrans) {
    if (MR::isNearZero(rDir)) {
        return;
    }

    TVec3f rot;
    TPos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, rDir, TVec3f(0.0f, 0.0f, 0.0f));
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
    for (u32 i = 0; i < _B9C; i++) {
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

    if (mMario->mMovementStates._4) {
        effectA = 1;
    }

    if (mMario->mMovementStates._F && mMario->mMovementStates._1) {
        effectA = 1;
    }

    if (mMario->mMovementStates._1 && mMario->_3F4 > 0.1f) {
        effectA = 1;
    }

    if (mMario->isStatusActive(MarioStatus_Flip)) {
        effectA = 1;
    }

    if (getMovementStates()._34 || getMovementStates()._35) {
        if (mMario->mWalkSpeed > 0.1f) {
            effectA = 1;
        }

        if (effectA != 0) {
            effectB = 1;
            effectA = 0;
        }
    }

    if (_3C0 || mPlayerMode == 1 || mMario->isStatusActive(MarioStatus_Swim) || mMario->isStatusActive(MarioStatus_FpView) || _934) {
        effectA = 0;
        effectB = 0;
        effectC = 0;
    }

    if (effectA) {
        playEffect("\213\244\222\312\215\273\211\214\203\214\203\170\203\213" /* 共通砂煙レベル */);
    } else if (!effectA) {
        if (mEffectFlags.mSmoke != 0) {
            stopEffect("\213\244\222\312\215\273\211\214\203\214\203\170\203\213" /* 共通砂煙レベル */);
        }
    }

    if (effectB && mEffectFlags.mSnow == 0) {
        playEffect("\220\341\211\214\203\214\203\170\203\213" /* 雪煙レベル */);
    } else if (!effectB && mEffectFlags.mSnow != 0) {
        stopEffect("\220\341\211\214\203\214\203\170\203\213" /* 雪煙レベル */);
    }

    mEffectFlags.mSmoke = effectA;
    mEffectFlags.mSnow = effectB;
    mEffectFlags._2 = effectC;

    effectA = 0;
    if (getMovementStates()._23 && !getMovementStates()._18 && getMovementStates()._1) {
        effectA = 1;
    }

    if (effectA) {
        playEffect("\213\244\222\312\203\130\203\212\203\142\203\166\215\342" /* 共通スリップ坂 */);
    } else if (!effectA) {
        if (mEffectFlags.mSlopeSlip != 0) {
            stopEffect("\213\244\222\312\203\130\203\212\203\142\203\166\215\342" /* 共通スリップ坂 */);
        }
    }

    mEffectFlags.mSlopeSlip = effectA;

    if ((mHealth <= 1 || mWaterLife <= 1) && !MR::isDemoActive() && !MR::isPowerStarGetDemoActive() && !MR::isGalaxyDarkCometAppearInCurrentStage() &&
        MR::isPermitSE() && isEnableNerveChange()) {
        playSound("\203\211\203\103\203\164\214\170\215\220" /* ライフ警告 */, -1);
    }

    s32 nearZero = 0;
    if (MR::isNearZero(_240)) {
        nearZero = 1;
    }

    if (!nearZero && mEffectFlags.mIsStationary) {
        playEffect("\211\106\222\210\227\126\211\152" /* 宇宙遊泳 */);
    } else if (nearZero && !mEffectFlags.mIsStationary) {
        stopEffect("\211\106\222\210\227\126\211\152" /* 宇宙遊泳 */);
    }

    mEffectFlags.mIsStationary = nearZero;

    if (!isJumping() && _934 == 0) {
        MR::deleteEffect(this, "\203\127\203\203\203\223\203\166\203\164\203\106\203\101\203\212\201\133" /* ジャンプフェアリー */);
    }

    mMarioEffect->doCubeEffect();

    effectC = 0;
    if (getMovementStates().jumping) {
        if (mPlayerMode == 4 && mMario->checkLvlA() && mMario->_402 != 0 && ((_37C & 3) == 0)) {
            if (mMario->mVerticalSpeed < 300.0f) {
                effectC = 1;
            } else if (mMario->mSwim->_1B2 != 0) {
                f32 swimSpeed = mMario->mSwim->_1B8;
                if (swimSpeed < 300.0f) {
                    effectC = 1;
                }
            }
        }
    }

    if (effectC) {
        playEffectRT("\221\256\220\253\203\156\203\140\225\227" /* 属性ハチ風 */, mMario->_368, mMario->mShadowPos);
    } else {
        if (mEffectFlags.mBeeWind) {
            stopEffect("\221\256\220\253\203\156\203\140\225\227" /* 属性ハチ風 */);
        }
    }

    mEffectFlags.mBeeWind = effectC;

    u32 i = 0;
    while (i < _B9E) {
        SmokeEffectEntry* entry = _BA0[i];
        if (entry->mTimer != 0) {
            entry->mTimer--;
            if (entry->mTimer == 0) {
                stopEffect(entry->mName);
            }
        }

        i++;
    }
}

MarioEffect::MarioEffect(MarioActor* pActor) : MarioModule(pActor) {
    _C = -1;
    _10 = -1;
    _14 = -1;
    _18 = 0;
    _1C.set(0, 0, 0, 0);
    _20.set(0, 0, 0, 0);
    PSMTXIdentity(_24.toMtxPtr());
}

void MarioEffect::playSwingEffect() {
    if (_C == 1) {
        switch (_10) {
        default:
            break;
        case 0:
            playEffectTrans("FlowerSpin", getShadowPos());
            break;
        }
    }

    Mario* mario = getPlayer();
    playEffectRT("\221\256\220\253\203\130\203\163\203\223" /* 属性スピン */, mario->_368, getTrans());
}

void MarioEffect::doCubeEffect() {
    Mario* mario = getPlayer();
    if (mario->mMovementStates.debugMode) {
        return;
    }

    s32 unsetArg = -1;
    _10 = unsetArg;
    _C = unsetArg;

    AreaObj* area = MR::getAreaObj("EffectCylinder", mActor->mPosition);
    if (area != nullptr) {
        s32 arg0 = MR::getAreaObjArg(area, 0);
        _10 = MR::getAreaObjArg(area, 1);
        _C = arg0;
    } else {
        _14 = unsetArg;
    }

    AreaObj* colorArea = MR::getAreaObj("SmokeEffectColorArea", mActor->mPosition);
    if (colorArea != nullptr) {
        const s32 prmR = MR::getAreaObjArg(colorArea, 0);
        const s32 prmG = MR::getAreaObjArg(colorArea, 1);
        const s32 prmB = MR::getAreaObjArg(colorArea, 2);
        _1C.set(prmR, prmG, prmB, 0);

        const s32 envR = MR::getAreaObjArg(colorArea, 3);
        const s32 envG = MR::getAreaObjArg(colorArea, 4);
        const s32 envB = MR::getAreaObjArg(colorArea, 5);
        _20.set(envR, envG, envB, 0);
        _18 = 1;
    } else {
        _18 = 0;
    }

    if (getPlayer()->_10._1) {
        playSwingEffect();
        getPlayer()->_10._1 = 0;
    }
}

bool Mario::taskOnEffectCheck(u32 flags) {
    if (flags & 0x2) {
        return mActor->checkEffectWaterColumn();
    }

    return true;
}

bool MarioActor::checkEffectWaterColumn() {
    if (MR::isBckStopped(_BA8)) {
        _BA8->kill();
        return false;
    }

    return true;
}

void MarioActor::stopSpinTicoEffect(bool force) {
    _948 = 0;
    _94C = 0;
    _94E = 0;
    _946 = 0;

    if (force) {
        stopEffectForce("\203\130\203\163\203\223\203\212\203\223\203\117" /* スピンリング */);
    } else {
        stopEffect("\203\130\203\163\203\223\203\212\203\223\203\117" /* スピンリング */);
    }
}

MarioEffect::~MarioEffect() {
}

void MultiEmitter::setGlobalScale(f32 scale, s32 a2) {
    TVec3f scaleVec(scale, scale, scale);
    setGlobalScale(scaleVec, a2);
}
