#include "Game/System/NebulaMods.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/GameEventFlagTable.hpp"

#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <cstring>
#include <initializer_list>

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
    struct Track {
        const char* mName;
        const char* mFile;
    };
    List< Track > sTracks;
    struct MapSpot {
        const char* mGalaxy;
        f32 mX;
        f32 mY;
        s32 mDome;
    };
    List< MapSpot > sMapSpots;
    struct StageTrack {
        const char* mStage;
        s32 mScenario;  // -1: this row is for an area
        s32 mArea;
        u32 mId;
    };
    List< StageTrack > sStageTracks;
    struct ModText {
        const char* mId;
        const char16_t* mWords;
    };
    List< ModText > sTexts;
    List< NebulaPowerUpClass > sPowerUps;
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
        if (pGalaxy->has_map_position != 0) {
            MapSpot spot = {added.mName, pGalaxy->map_position[0], pGalaxy->map_position[1], pGalaxy->dome};
            sMapSpots.add(spot);
            // A layout's pane names are short: this one is only ever looked for by the game.
            if (added.mPane == nullptr) {
                char paneName[16];
                nebula_snprintf(paneName, sizeof(paneName), "NebMap%d", sMapSpots.mCount);
                added.mPane = copyText(paneName);
            }
        }
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

    bool isGameSoundName(const char* pName) {
        for (const char* pPrefix : {"SE_", "BGM_", "MBGM_", "STM_", "ME_"}) {
            if (strncmp(pName, pPrefix, strlen(pPrefix)) == 0) {
                return true;
            }
        }
        return false;
    }

    int32_t apiAddMusic(const char* pName, const char* pFile) {
        if (pName == nullptr || pFile == nullptr || pName[0] == '\0' || pFile[0] == '\0' || isGameSoundName(pName)) {
            return -1;
        }
        // The file as the game has it: each of these ends up a stream of this name.
        const char* pEnd = strrchr(pFile, '.');
        const size_t stem = pEnd != nullptr ? static_cast< size_t >(pEnd - pFile) : strlen(pFile);
        const bool rooted = pFile[0] == '/';
        char* pPath = new char[stem + 6];
        pPath[0] = '/';
        memcpy(pPath + 1, pFile + (rooted ? 1 : 0), stem - (rooted ? 1 : 0));
        memcpy(pPath + 1 + stem - (rooted ? 1 : 0), ".ast", 5);
        for (int i = 0; i < sTracks.mCount; i++) {
            if (strcmp(sTracks.mItems[i].mName, pName) == 0) {
                sTracks.mItems[i].mFile = pPath;
                return 0;
            }
        }
        if (sTracks.mCount >= 0x10000 - static_cast< int >(NebulaMods::cFirstTrackWave)) {
            return -1;
        }
        Track track = {copyText(pName), pPath};
        sTracks.add(track);
        return 0;
    }

    int32_t apiAddStageTrack(const char* pStage, s32 scenario, s32 area, const char* pName) {
        if (pStage == nullptr || pName == nullptr) {
            return -1;
        }
        const u32 id = NebulaMods::trackId(pName);
        if (id == static_cast< u32 >(-1)) {
            return -1;
        }
        StageTrack row = {copyText(pStage), scenario, area, id};
        sStageTracks.add(row);
        return 0;
    }

    int32_t apiSetStageMusic(const char* pStage, int32_t scenario, const char* pName) {
        return scenario >= 0 ? apiAddStageTrack(pStage, scenario, -1, pName) : -1;
    }

    int32_t apiSetAreaMusic(const char* pStage, int32_t area, const char* pName) {
        return area >= 0 ? apiAddStageTrack(pStage, -1, area, pName) : -1;
    }

    int32_t apiPlayMusic(const char* pName) {
        if (pName == nullptr || sStageFrames <= 0) {
            return 0;
        }
        if (AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName).isAnonymous()) {
            return 0;
        }
        return MR::startStageBGM(pName, false) != nullptr ? 1 : 0;
    }

    int32_t apiSetEventFlag(const char* pName, int32_t kind, int32_t a, int32_t b, const char* pFirst, const char* pSecond) {
        if (pName == nullptr || pName[0] == '\0' || kind < 0 || kind > 255 || a < 0 || a > 255 || b < 0 || b > 255) {
            return -1;
        }
        GameEventFlagTable::setFlag(copyText(pName), static_cast< u8 >(kind), static_cast< u8 >(a), static_cast< u8 >(b), copyText(pFirst), copyText(pSecond));
        return 0;
    }

    int32_t apiSetCometSchedule(int32_t dome, const char* const* ppGalaxies, const int32_t* pCoins, int32_t count) {
        if (dome < 1 || dome > 6 || ppGalaxies == nullptr || pCoins == nullptr || count <= 0) {
            return -1;
        }
        GalaxyCometTimePaper* pPaper = new GalaxyCometTimePaper[count + 1];
        for (s32 i = 0; i < count; i++) {
            pPaper[i].mGalaxyName = copyText(ppGalaxies[i]);
            pPaper[i].mCategory = pCoins[i];
        }
        pPaper[count].mGalaxyName = nullptr;
        pPaper[count].mCategory = 0;
        GalaxyCometScheduler::setTimePaper(dome - 1, pPaper);
        return 0;
    }

    int32_t apiPlayMovie(const char* pFile) {
        if (pFile == nullptr || pFile[0] == '\0' || sStageFrames <= 0) {
            return 0;
        }
        // The player holds on to the name.
        char path[256];
        nebula_snprintf(path, sizeof(path), pFile[0] == '/' ? "%s" : "/%s", pFile);
        if (GameAssets::find(path) < 0) {
            return 0;
        }
        if (!MR::isExistSceneObj(SceneObj_MoviePlayerSimple)) {
            return 0;
        }
        MR::startMoviePlayer(copyText(path));
        return 1;
    }

    void apiStopMovie() {
        if (sStageFrames > 0 && MR::isActiveMoviePlayer()) {
            MR::stopMoviePlayer();
        }
    }

    int32_t apiIsMoviePlaying() {
        return sStageFrames > 0 && MR::isActiveMoviePlayer() && MR::isMoviePlayerPlaying() ? 1 : 0;
    }

    void apiStopMusic(int32_t frames) {
        if (sStageFrames > 0 && frames >= 0) {
            MR::stopStageBGM(frames);
        }
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

    int32_t apiAddPowerUp(const NebulaPowerUpClass* pClass) {
        if (pClass == nullptr || pClass->size < sizeof(NebulaPowerUpClass)) {
            return -1;
        }
        NebulaPowerUpClass power = *pClass;
        power.name = copyText(pClass->name);
        power.music = copyText(pClass->music);
        sPowerUps.add(power);
        return NebulaMods::cFirstPowerUp + sPowerUps.mCount - 1;
    }

    const NebulaPowerUpClass* powerUpOf(u16 mode) {
        const int index = static_cast< int >(mode) - NebulaMods::cFirstPowerUp;
        return index >= 0 && index < sPowerUps.mCount ? &sPowerUps.mItems[index] : nullptr;
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

    void apiGetGravity(NebulaActor* pActor, f32* pOut) {
        const TVec3f& rDown = asActor(pActor)->mGravity;
        pOut[0] = rDown.x;
        pOut[1] = rDown.y;
        pOut[2] = rDown.z;
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
        apiAddPowerUp,
        apiGetGravity,
        apiAddMusic,
        apiSetStageMusic,
        apiSetAreaMusic,
        apiPlayMusic,
        apiStopMusic,
        apiPlayMovie,
        apiStopMovie,
        apiIsMoviePlaying,
        apiSetEventFlag,
        apiSetCometSchedule,
    };
}  // namespace

extern "C" NEBULA_GAME_API const NebulaModApi* nebula_game_mod_api(void) {
    return &sApi;
}

namespace NebulaMods {
    bool hasCode() {
        for (int i = 0; i < NEBULA_MOD_EVENT_COUNT; i++) {
            if (sWanted[i]) {
                return true;
            }
        }
        return false;
    }

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

    s32 powerUpStart(u16 mode, LiveActor* pPlayer) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower == nullptr) {
            return 0;
        }
        if (pPower->music != nullptr) {
            MR::startSubBGM(pPower->music, false);
        }
        if (pPower->color_frame > 0) {
            MR::startBtp(pPlayer, "ColorChange");
            MR::setBtpFrameAndStop(pPlayer, static_cast< f32 >(pPower->color_frame));
        }
        if (pPower->start != nullptr) {
            pPower->start(pPower->user);
        }
        return pPower->frames;
    }

    void powerUpUpdate(u16 mode) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower != nullptr && pPower->update != nullptr) {
            pPower->update(pPower->user);
        }
    }

    void powerUpEnd(u16 mode, LiveActor* pPlayer) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower == nullptr) {
            return;
        }
        if (pPower->music != nullptr) {
            MR::stopSubBGM(30);
        }
        if (pPower->color_frame > 0) {
            MR::startBtp(pPlayer, "ColorChange");
            MR::setBtpFrameAndStop(pPlayer, 0.0f);
        }
        if (pPower->end != nullptr) {
            pPower->end(pPower->user);
        }
    }

    const char16_t* text(const char* pId) {
        for (int i = sTexts.mCount - 1; i >= 0; i--) {
            if (strcmp(sTexts.mItems[i].mId, pId) == 0) {
                return sTexts.mItems[i].mWords;
            }
        }
        return nullptr;
    }

    u32 trackId(const char* pName) {
        for (int i = 0; i < sTracks.mCount; i++) {
            if (strcmp(sTracks.mItems[i].mName, pName) == 0) {
                return 0x02000000u | (cFirstTrackWave + static_cast< u32 >(i));
            }
        }
        return static_cast< u32 >(-1);
    }

    s32 trackFile(u32 wave) {
        const u32 index = wave - cFirstTrackWave;
        return index < static_cast< u32 >(sTracks.mCount) ? GameAssets::find(sTracks.mItems[index].mFile) : -1;
    }

    u32 stageTrack(const char* pStage, s32 scenario) {
        u32 forAll = static_cast< u32 >(-1);
        for (int i = sStageTracks.mCount - 1; i >= 0; i--) {
            const StageTrack& rRow = sStageTracks.mItems[i];
            if (rRow.mScenario < 0 || strcmp(rRow.mStage, pStage) != 0) {
                continue;
            }
            if (rRow.mScenario == scenario) {
                return rRow.mId;
            }
            if (rRow.mScenario == 0 && forAll == static_cast< u32 >(-1)) {
                forAll = rRow.mId;
            }
        }
        return forAll;
    }

    u32 areaTrack(const char* pStage, s32 area) {
        for (int i = sStageTracks.mCount - 1; i >= 0; i--) {
            const StageTrack& rRow = sStageTracks.mItems[i];
            if (rRow.mArea == area && strcmp(rRow.mStage, pStage) == 0) {
                return rRow.mId;
            }
        }
        return static_cast< u32 >(-1);
    }

    bool mapSpot(const char* pGalaxy, f32* pX, f32* pY, s32* pDome) {
        for (int i = sMapSpots.mCount - 1; i >= 0; i--) {
            if (strcmp(sMapSpots.mItems[i].mGalaxy, pGalaxy) == 0) {
                *pX = sMapSpots.mItems[i].mX;
                *pY = sMapSpots.mItems[i].mY;
                *pDome = sMapSpots.mItems[i].mDome;
                return true;
            }
        }
        return false;
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
