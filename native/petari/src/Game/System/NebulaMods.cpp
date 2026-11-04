#include "Game/System/NebulaMods.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GameEventFlagTable.hpp"

#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include <nebula_game_api.h>
#include <cstring>

namespace NebulaMods {
    bool sWanted[NEBULA_MOD_EVENT_COUNT];
}  // namespace NebulaMods

namespace {
    template < typename T >
    class List {
    public:
        void add(const T& rItem) {
            if (mCount == mCapacity) {
                const int capacity = mCapacity == 0 ? 8 : mCapacity * 2;
                T* pItems = new T[capacity];
                for (int i = 0; i < mCount; i++) {
                    pItems[i] = mItems[i];
                }
                delete[] mItems;
                mItems = pItems;
                mCapacity = capacity;
            }
            mItems[mCount++] = rItem;
        }

        T* mItems = nullptr;
        int mCount = 0;
        int mCapacity = 0;
    };

    struct Subscriber {
        NebulaModCallback mCallback;
        void* mUser;
    };

    // A plugin's object, by the name a level places it under.
    struct ObjectEntry {
        const char* mName;
        const char* mArchive;
        NebulaObjectClass mClass;
        NebulaMods::CreatorFuncPtr mCreator;
    };

    List< Subscriber > sSubscribers[NEBULA_MOD_EVENT_COUNT];
    List< ObjectEntry > sObjects;
    struct HungryLuma {
        s32 mSlot;
        s32 mPrice;
    };
    List< HungryLuma > sHungryLumas;
    struct ModText {
        const char* mId;
        const char16_t* mWords;
    };
    List< ModText > sTexts;
    int sStageFrames = 0;

    const char* copyText(const char* pText) {
        if (pText == nullptr) {
            return nullptr;
        }
        const size_t length = strlen(pText) + 1;
        char* pCopy = new char[length];
        memcpy(pCopy, pText, length);
        return pCopy;
    }

    const ObjectEntry* findObject(const char* pName) {
        for (int i = 0; i < sObjects.mCount; i++) {
            if (strcmp(sObjects.mItems[i].mName, pName) == 0) {
                return &sObjects.mItems[i];
            }
        }
        return nullptr;
    }

    NebulaActor* asHandle(LiveActor* pActor) {
        return reinterpret_cast< NebulaActor* >(pActor);
    }

    LiveActor* asActor(NebulaActor* pActor) {
        return reinterpret_cast< LiveActor* >(pActor);
    }

    class PluginActor : public LiveActor {
    public:
        PluginActor(const char* pName, const NebulaObjectClass* pClass) : LiveActor(pName), mClass(pClass) {}

        virtual void init(const JMapInfoIter& rIter) {
            if (rIter.isValid()) {
                MR::initDefaultPos(this, rIter);
            }
            if (mClass->archive != nullptr) {
                initModelManagerWithAnm(mClass->archive, nullptr, false);
                MR::connectToSceneMapObj(this);
            } else {
                MR::connectToSceneMapObjMovement(this);
            }
            initHitSensor(1);
            const TVec3f offset(0.0f, 0.0f, 0.0f);
            MR::addHitSensorEnemy(this, "body", 8, mClass->radius, offset);
            initBinder(mClass->radius, 0.0f, 0);
            MR::onCalcGravity(this);
            MR::invalidateClipping(this);
            initSound(4, false);
            // Its effects are the AutoEffectList rows of its own name.
            if (mClass->archive != nullptr) {
                initEffectKeeper(0, mClass->name, false);
            }
            if (mClass->start != nullptr) {
                mClass->start(asHandle(this), mClass->user);
            }
            makeActorAppeared();
        }

        virtual void control() {
            if (mClass->update != nullptr) {
                mClass->update(asHandle(this), mClass->user);
            }
        }

        virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
            if (mClass->touch_player != nullptr && MR::isSensorPlayer(pReceiver)) {
                mClass->touch_player(asHandle(this), mClass->user);
            }
        }

        virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
            return mClass->attacked != nullptr && mClass->attacked(asHandle(this), mClass->user) != 0;
        }

        const NebulaObjectClass* mClass;
    };

    NameObj* createPluginActor(const char* pName) {
        const ObjectEntry* pEntry = findObject(pName);
        return pEntry != nullptr ? new PluginActor(pName, &pEntry->mClass) : nullptr;
    }

    int32_t apiSubscribe(NebulaModEvent event, NebulaModCallback callback, void* user) {
        if (static_cast< u32 >(event) >= NEBULA_MOD_EVENT_COUNT || callback == nullptr) {
            return -1;
        }
        Subscriber subscriber = {callback, user};
        sSubscribers[event].add(subscriber);
        NebulaMods::sWanted[event] = true;
        return 0;
    }

    int32_t apiRegisterObject(const NebulaObjectClass* pObject) {
        if (pObject == nullptr || pObject->size < sizeof(NebulaObjectClass) || pObject->name == nullptr || findObject(pObject->name) != nullptr) {
            return -1;
        }
        ObjectEntry entry;
        entry.mName = copyText(pObject->name);
        entry.mArchive = copyText(pObject->archive);
        entry.mClass = *pObject;
        entry.mClass.name = entry.mName;
        entry.mClass.archive = entry.mArchive;
        entry.mCreator = createPluginActor;
        sObjects.add(entry);
        return 0;
    }

    int32_t apiRegisterCreator(const char* pName, void* (*create)(const char*), const char* pArchive) {
        if (pName == nullptr || create == nullptr || findObject(pName) != nullptr) {
            return -1;
        }
        ObjectEntry entry = {};
        entry.mName = copyText(pName);
        entry.mArchive = copyText(pArchive);
        entry.mCreator = reinterpret_cast< NebulaMods::CreatorFuncPtr >(create);
        sObjects.add(entry);
        return 0;
    }

    int32_t apiAddGalaxy(const NebulaGalaxy* pGalaxy) {
        if (pGalaxy == nullptr || pGalaxy->size < sizeof(NebulaGalaxy) || pGalaxy->name == nullptr) {
            return -1;
        }
        NebulaDataTables::AddedGalaxy added = {};
        added.mName = copyText(pGalaxy->name);
        added.mPane = copyText(pGalaxy->map_pane);
        for (int i = 0; i < 3; i++) {
            added.mConditions[i] = copyText(pGalaxy->open_condition[i]);
        }
        added.mStarsToOpen = static_cast< u8 >(pGalaxy->stars_to_open);
        added.mDome = static_cast< u8 >(pGalaxy->dome);
        if (!GameEventFlagTable::addGalaxy(added.mName)) {
            return -1;
        }
        NebulaDataTables::addGalaxy(added);
        return 0;
    }

    int32_t apiAddHungryLuma(const char* pGalaxy, int32_t slot, int32_t starBits) {
        if (pGalaxy == nullptr || slot < 0 || slot > 200 || starBits <= 0 || starBits > 32767) {
            return -1;
        }
        if (!GameEventFlagTable::addHungryLuma(copyText(pGalaxy), slot, starBits)) {
            return -1;
        }
        HungryLuma luma = {slot, starBits};
        sHungryLumas.add(luma);
        return 0;
    }

    int32_t apiAddText(const char* pId, const char* pUtf8) {
        if (pId == nullptr || pUtf8 == nullptr) {
            return -1;
        }
        const size_t length = strlen(pUtf8);
        char16_t* pWords = new char16_t[length + 1];
        size_t out = 0;
        for (size_t i = 0; i < length;) {
            const u8 lead = static_cast< u8 >(pUtf8[i]);
            const size_t extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
            u32 code = extra == 0 ? lead : extra == 1 ? lead & 0x1F : extra == 2 ? lead & 0x0F : lead & 0x07;
            for (size_t c = 1; c <= extra && i + c < length; c++) {
                code = (code << 6) | (static_cast< u8 >(pUtf8[i + c]) & 0x3F);
            }
            i += extra + 1;
            if (code >= 0x10000) {
                code -= 0x10000;
                pWords[out++] = static_cast< char16_t >(0xD800 | (code >> 10));
                pWords[out++] = static_cast< char16_t >(0xDC00 | (code & 0x3FF));
            } else {
                pWords[out++] = static_cast< char16_t >(code);
            }
        }
        pWords[out] = 0;
        ModText text = {copyText(pId), pWords};
        sTexts.add(text);
        return 0;
    }

    NebulaActor* apiPlayer() {
        return sStageFrames > 0 ? asHandle(MarioAccess::getPlayerActor()) : nullptr;
    }

    void apiGetPosition(NebulaActor* pActor, f32* pOut) {
        const TVec3f& rAt = asActor(pActor)->mPosition;
        pOut[0] = rAt.x;
        pOut[1] = rAt.y;
        pOut[2] = rAt.z;
    }

    void apiSetPosition(NebulaActor* pActor, const f32* pIn) {
        const TVec3f at(pIn[0], pIn[1], pIn[2]);
        if (pActor == apiPlayer()) {
            MR::setPlayerPos(at);
        } else {
            asActor(pActor)->mPosition.set(at);
        }
    }

    void apiGetVelocity(NebulaActor* pActor, f32* pOut) {
        const TVec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pOut[0] = pVelocity->x;
        pOut[1] = pVelocity->y;
        pOut[2] = pVelocity->z;
    }

    void apiSetVelocity(NebulaActor* pActor, const f32* pIn) {
        TVec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pVelocity->set(pIn[0], pIn[1], pIn[2]);
    }

    int32_t apiValue(NebulaModValue what) {
        MarioActor* pPlayer = sStageFrames > 0 ? MarioAccess::getPlayerActor() : nullptr;
        switch (what) {
        case NEBULA_VALUE_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mHealth) : 0;
        case NEBULA_VALUE_MAX_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mMaxHealth) : 0;
        case NEBULA_VALUE_POWER_UP: return pPlayer != nullptr ? pPlayer->mPlayerMode : 0;
        case NEBULA_VALUE_ON_GROUND: return pPlayer != nullptr && MR::isOnGroundPlayer() ? 1 : 0;
        case NEBULA_VALUE_STAR_BITS: return MR::getStarPieceNum();
        case NEBULA_VALUE_COINS: return sStageFrames > 0 ? MR::getCoinNum() : 0;
        case NEBULA_VALUE_LIVES: return MR::getPlayerLeft();
        case NEBULA_VALUE_MISSION: return sStageFrames > 0 ? MR::getCurrentScenarioNo() : 0;
        default: return 0;
        }
    }

    void apiSetValue(NebulaModSet what, int32_t value) {
        MarioActor* pPlayer = sStageFrames > 0 ? MarioAccess::getPlayerActor() : nullptr;
        if (pPlayer == nullptr) {
            return;
        }
        switch (what) {
        case NEBULA_SET_LIFE: pPlayer->mHealth = static_cast< u32 >(value); break;
        case NEBULA_SET_MAX_LIFE: pPlayer->mMaxHealth = static_cast< u32 >(value); break;
        case NEBULA_SET_POWER_UP: pPlayer->setPlayerMode(static_cast< u16 >(value), false); break;
        case NEBULA_SET_ADD_COINS: MR::incCoin(value); break;
        case NEBULA_SET_ADD_STAR_BITS: MR::addStarPiece(value); break;
        case NEBULA_SET_ADD_LIVES:
            for (s32 i = 0; i < value; i++) {
                MR::incPlayerLeft();
            }
            break;
        default: break;
        }
    }

    void apiKillPlayer() {
        if (sStageFrames > 0) {
            MarioAccess::forceKill(0, 0);
        }
    }

    void apiJumpPlayer(const f32* pVelocity) {
        if (sStageFrames > 0) {
            MarioAccess::forceJump(TVec3f(pVelocity[0], pVelocity[1], pVelocity[2]), 0);
        }
    }

    void apiAppear(NebulaActor* pActor) {
        asActor(pActor)->appear();
    }

    void apiKill(NebulaActor* pActor) {
        asActor(pActor)->kill();
    }

    int32_t apiIsDead(NebulaActor* pActor) {
        return MR::isDead(asActor(pActor)) ? 1 : 0;
    }

    void apiShowModel(NebulaActor* pActor, int32_t show) {
        if (asActor(pActor)->mModelManager == nullptr) {
            return;
        }
        if (show != 0) {
            MR::showModel(asActor(pActor));
        } else {
            MR::hideModel(asActor(pActor));
        }
    }

    int32_t apiPlay(NebulaActor* pActor, NebulaModPlay what, const char* pName) {
        LiveActor* pLive = asActor(pActor);
        switch (what) {
        case NEBULA_PLAY_ANIMATION:
            if (pLive->mModelManager == nullptr) {
                return 0;
            }
            MR::startAction(pLive, pName);
            return 1;
        case NEBULA_PLAY_EFFECT:
            if (pLive->mEffectKeeper == nullptr || !pLive->mEffectKeeper->isRegisteredEmitter(pName)) {
                return 0;
            }
            return MR::emitEffect(pLive, pName) != nullptr ? 1 : 0;
        case NEBULA_PLAY_SOUND:
            if (pLive->mSoundObject == nullptr) {
                return 0;
            }
            return MR::startSound(pLive, pName) != nullptr ? 1 : 0;
        case NEBULA_PLAY_PLAYER_ANIMATION:
            if (sStageFrames == 0) {
                return 0;
            }
            MarioAccess::getPlayerActor()->changeAnimationNonStop(pName);
            return 1;
        default:
            return 0;
        }
    }

    const char* apiActorName(NebulaActor* pActor) {
        return asActor(pActor)->mName;
    }

    const char* apiStageName() {
        return sStageFrames > 0 ? MR::getCurrentStageName() : nullptr;
    }

    NebulaActor* apiSpawn(const char* pName, const f32* pAt) {
        const ObjectEntry* pEntry = findObject(pName);
        if (pEntry == nullptr || (pEntry->mArchive != nullptr && !MR::isLoadedObjectOrLayoutArchive(pEntry->mArchive))) {
            return nullptr;
        }
        NameObj* pObject = pEntry->mCreator(pEntry->mName);
        if (pObject == nullptr) {
            return nullptr;
        }
        LiveActor* pActor = static_cast< LiveActor* >(pObject);
        pActor->mPosition.set(pAt[0], pAt[1], pAt[2]);
        pActor->initWithoutIter();
        return asHandle(pActor);
    }

    uint32_t apiButtonsHeld() {
        return (MR::getPlayerHeldJump() ? NEBULA_BUTTON_JUMP : 0u) | (MR::getPlayerHeldFire() ? NEBULA_BUTTON_SPIN : 0u) |
               (MR::getPlayerHeldCrouch() ? NEBULA_BUTTON_CROUCH : 0u) | (MR::getPlayerHeldCamera() ? NEBULA_BUTTON_CAMERA : 0u);
    }

    uint32_t apiButtonsPressed() {
        return (MR::getPlayerPressedJump() ? NEBULA_BUTTON_JUMP : 0u) | (MR::getPlayerPressedFire() ? NEBULA_BUTTON_SPIN : 0u) |
               (MR::getPlayerPressedCrouch() ? NEBULA_BUTTON_CROUCH : 0u) | (MR::getPlayerPressedCamera() ? NEBULA_BUTTON_CAMERA : 0u);
    }

    void apiStick(f32* pX, f32* pY) {
        *pX = MR::getPlayerStickX();
        *pY = MR::getPlayerStickY();
    }

    void apiShakeCamera() {
        if (sStageFrames > 0) {
            MR::shakeCameraNormal();
        }
    }

    // The fields the host fills are its own: log, option, hook, find_function.
    const NebulaModApi sApi = {
        sizeof(NebulaModApi),
        NEBULA_MOD_API_VERSION,
        NEBULA_GAME_ABI,
        nullptr,
        nullptr,
        apiSubscribe,
        apiRegisterObject,
        apiRegisterCreator,
        nullptr,
        nullptr,
        apiPlayer,
        apiGetPosition,
        apiSetPosition,
        apiGetVelocity,
        apiSetVelocity,
        apiValue,
        apiSetValue,
        apiKillPlayer,
        apiJumpPlayer,
        apiAppear,
        apiKill,
        apiIsDead,
        apiShowModel,
        apiPlay,
        apiActorName,
        apiStageName,
        apiSpawn,
        apiButtonsHeld,
        apiButtonsPressed,
        apiStick,
        apiShakeCamera,
        apiAddGalaxy,
        apiAddHungryLuma,
        apiAddText,
    };
}  // namespace

extern "C" NEBULA_GAME_API const NebulaModApi* nebula_game_mod_api(void) {
    return &sApi;
}

namespace NebulaMods {
    s32 dispatch(NebulaModEvent event, s32 value, void* pActor, const char* pName) {
        const List< Subscriber >& rSubscribers = sSubscribers[event];
        NebulaModEventData data = {value, static_cast< NebulaActor* >(pActor), pName};
        for (int i = 0; i < rSubscribers.mCount; i++) {
            data.value = rSubscribers.mItems[i].mCallback(&data, rSubscribers.mItems[i].mUser);
        }
        return data.value;
    }

    StageFrame::StageFrame() {
        sStageFrames++;
        fire(NEBULA_MOD_EVENT_STAGE_FRAME);
    }

    StageFrame::~StageFrame() {
        sStageFrames--;
    }

    const char16_t* text(const char* pId) {
        for (int i = sTexts.mCount - 1; i >= 0; i--) {
            if (strcmp(sTexts.mItems[i].mId, pId) == 0) {
                return sTexts.mItems[i].mWords;
            }
        }
        return nullptr;
    }

    s32 hungryLumaPrice(s32 slot) {
        for (int i = sHungryLumas.mCount - 1; i >= 0; i--) {
            if (sHungryLumas.mItems[i].mSlot == slot) {
                return sHungryLumas.mItems[i].mPrice;
            }
        }
        return -1;
    }

    s32 hungryLumaSlots() {
        s32 slots = 0;
        for (int i = 0; i < sHungryLumas.mCount; i++) {
            if (sHungryLumas.mItems[i].mSlot >= slots) {
                slots = sHungryLumas.mItems[i].mSlot + 1;
            }
        }
        return slots;
    }

    CreatorFuncPtr findCreator(const char* pName) {
        if (sObjects.mCount == 0) {
            return nullptr;
        }
        const ObjectEntry* pEntry = findObject(pName);
        return pEntry != nullptr ? pEntry->mCreator : nullptr;
    }

    void addArchives(NameObjArchiveListCollector* pCollector, const char* pName) {
        if (sObjects.mCount == 0) {
            return;
        }
        const ObjectEntry* pEntry = findObject(pName);
        if (pEntry != nullptr && pEntry->mArchive != nullptr) {
            pCollector->addArchive(pEntry->mArchive);
        }
    }
}  // namespace NebulaMods
