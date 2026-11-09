#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/GameEventFlagTable.hpp"

#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include <nebula_format.h>
#include <nebula_module.h>
#include <nebula_game_api.h>
#include <cstddef>
#include <cstring>
#include <initializer_list>

namespace NebulaMods {
    bool sWanted[NEBULA_MOD_EVENT_COUNT];
    bool sHasStageRules = false;
    bool sHasSoundEffects = false;
}  // namespace NebulaMods

namespace {
    // A list grows in its owner's arena; a static one (outside every arena, it lasts the run) in the root arena,
    // whichever arena a plugin's call finds current.
    template < typename T >
    class List {
    public:
        void add(const T& rItem) {
            if (mCount == mCapacity) {
                const int capacity = mCapacity == 0 ? 8 : mCapacity * 2;
                NebulaArena* const pArena = NebulaArena::findFromRoot(this);
                T* pItems = new (pArena != nullptr ? pArena : NebulaArena::root(), 0) T[capacity];
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

    // A plugin's text kept: for the run (the root arena: tables, and names the next scene reads), or with pArena.
    const char* copyText(const char* pText, NebulaArena* pArena = NebulaArena::root()) {
        if (pText == nullptr) {
            return nullptr;
        }
        const size_t length = strlen(pText) + 1;
        char* pCopy = new (pArena, 0) char[length];
        memcpy(pCopy, pText, length);
        return pCopy;
    }

    const ObjectEntry* findObject(const char* pName) {
        for (int i = 0; i < sObjects.mCount; i++) {
            if (MR::isEqualString(sObjects.mItems[i].mName, pName)) {
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

    // Set by the first object made: how to tell one of ours from any other actor a plugin holds.
    void* sPluginActorTable = nullptr;

    class PluginActor : public LiveActor {
    public:
        PluginActor(const char* pName, const NebulaObjectClass* pClass)
            : LiveActor(pName), mClass(pClass), mState(0), mStateFrames(0), mUseFront(false), mFront(0.0f, 0.0f, 1.0f) {
            for (int i = 0; i < 8; i++) {
                mArgs[i] = -1;
            }
            sPluginActorTable = *reinterpret_cast< void** >(this);
        }

        virtual void init(const TableIter& rIter) {
            if (rIter.isValid()) {
                MR::initDefaultPos(this, rIter);
                MR::getPlacementArg0NoInit(rIter, &mArgs[0]);
                MR::getPlacementArg1NoInit(rIter, &mArgs[1]);
                MR::getPlacementArg2NoInit(rIter, &mArgs[2]);
                MR::getPlacementArg3NoInit(rIter, &mArgs[3]);
                MR::getPlacementArg4NoInit(rIter, &mArgs[4]);
                MR::getPlacementArg5NoInit(rIter, &mArgs[5]);
                MR::getPlacementArg6NoInit(rIter, &mArgs[6]);
                MR::getPlacementArg7NoInit(rIter, &mArgs[7]);
                MR::useStageSwitchWriteA(this, rIter);
                MR::useStageSwitchWriteB(this, rIter);
                MR::useStageSwitchWriteDead(this, rIter);
                MR::useStageSwitchReadAppear(this, rIter);
                if (MR::isConnectedWithRail(rIter)) {
                    initRailRider(rIter);
                }
            }
            if (mClass->archive != nullptr) {
                initModelManagerWithAnm(mClass->archive, nullptr, false);
                switch (mClass->kind) {
                case NEBULA_OBJECT_MAP:
                    if (mClass->collision != 0) {
                        MR::connectToSceneCollisionMapObj(this);
                    } else {
                        MR::connectToSceneMapObj(this);
                    }
                    break;
                case NEBULA_OBJECT_NPC: MR::connectToSceneNpc(this); break;
                case NEBULA_OBJECT_RIDE: MR::connectToSceneRide(this); break;
                case NEBULA_OBJECT_DECORATION: MR::connectToSceneMapObjDecoration(this); break;
                default:
                    if (mClass->collision != 0) {
                        MR::connectToSceneCollisionEnemy(this);
                    } else {
                        MR::connectToSceneMapObj(this);
                    }
                    break;
                }
            } else {
                MR::connectToSceneMapObjMovement(this);
            }
            initHitSensor(1);
            const Vec3f offset(0.0f, 0.0f, 0.0f);
            HitSensor* pSensor = nullptr;
            switch (mClass->kind) {
            case NEBULA_OBJECT_MAP: pSensor = MR::addHitSensorMapObj(this, "body", 8, mClass->radius, offset); break;
            case NEBULA_OBJECT_NPC: pSensor = MR::addHitSensorNpc(this, "body", 8, mClass->radius, offset); break;
            case NEBULA_OBJECT_RIDE: pSensor = MR::addHitSensorRide(this, "body", 8, mClass->radius, offset); break;
            default: pSensor = MR::addHitSensorEnemy(this, "body", 8, mClass->radius, offset); break;
            }
            if (mClass->kind == NEBULA_OBJECT_DECORATION) {
                MR::invalidateHitSensors(this);
            }
            initBinder(mClass->radius, 0.0f, 0);
            MR::onCalcGravity(this);
            if (mClass->collision != 0 && mClass->archive != nullptr) {
                MR::initCollisionParts(this, mClass->archive, pSensor, nullptr);
            }
            if (mClass->shadow_radius > 0.0f) {
                MR::initShadowVolumeSphere(this, mClass->shadow_radius);
            }
            if (mClass->clip_radius > 0.0f) {
                MR::setClippingTypeSphere(this, mClass->clip_radius);
            } else {
                MR::invalidateClipping(this);
            }
            initSound(4, false);
            // Its effects are the AutoEffectList rows of its own name.
            if (mClass->archive != nullptr) {
                initEffectKeeper(0, mClass->name);
            }
            if (mClass->start != nullptr) {
                mClass->start(asHandle(this), mClass->user);
            }
            makeActorAppeared();
        }

        virtual void appear() {
            LiveActor::appear();
            if (mClass->appeared != nullptr) {
                mClass->appeared(asHandle(this), mClass->user);
            }
        }

        virtual void kill() {
            LiveActor::kill();
            if (mClass->killed != nullptr) {
                mClass->killed(asHandle(this), mClass->user);
            }
        }

        virtual void control() {
            mStateFrames++;
            if (mClass->update != nullptr) {
                mClass->update(asHandle(this), mClass->user);
            }
        }

        virtual void calcAndSetBaseMtx() {
            if (!mUseFront) {
                LiveActor::calcAndSetBaseMtx();
                return;
            }
            Vec3f up(-mGravity.x, -mGravity.y, -mGravity.z);
            if (MR::isNearZero(up)) {
                up.set(0.0f, 1.0f, 0.0f);
            }
            Pos3f base;
            MR::makeMtxUpFrontPos(&base, up, mFront, mPosition);
            MR::setBaseTRMtx(this, base);
        }

        virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
            if (mClass->touch_player != nullptr && MR::isSensorPlayer(pReceiver)) {
                mClass->touch_player(asHandle(this), mClass->user);
            }
        }

        virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
            if (mClass->attacked_by != nullptr) {
                s32 how = NEBULA_ATTACK_OTHER;
                if (MR::isMsgPlayerSpinAttack(msg)) {
                    how = NEBULA_ATTACK_SPIN;
                } else if (MR::isMsgPlayerTrample(msg)) {
                    how = NEBULA_ATTACK_STOMP;
                } else if (MR::isMsgPlayerHipDrop(msg)) {
                    how = NEBULA_ATTACK_GROUND_POUND;
                } else if (MR::isMsgStarPieceAttack(msg)) {
                    how = NEBULA_ATTACK_STAR_BIT;
                } else if (MR::isMsgFireBallAttack(msg)) {
                    how = NEBULA_ATTACK_FIRE;
                } else if (MR::isMsgInvincibleAttack(msg)) {
                    how = NEBULA_ATTACK_INVINCIBLE;
                } else if (MR::isMsgJetTurtleAttack(msg)) {
                    how = NEBULA_ATTACK_SHELL;
                }
                return mClass->attacked_by(asHandle(this), how, mClass->user) != 0;
            }
            return mClass->attacked != nullptr && mClass->attacked(asHandle(this), mClass->user) != 0;
        }

        const NebulaObjectClass* mClass;
        s32 mState;
        s32 mStateFrames;
        s32 mArgs[8];
        bool mUseFront;
        Vec3f mFront;
        void* mData = nullptr;
    };

    PluginActor* asPluginActor(NebulaActor* pActor) {
        LiveActor* pLive = asActor(pActor);
        return pLive != nullptr && *reinterpret_cast< void** >(pLive) == sPluginActorTable ? static_cast< PluginActor* >(pLive) : nullptr;
    }

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
        // A class of an older header ends at `attacked_by`, and the rest is zero.
        if (pObject == nullptr || pObject->size < offsetof(NebulaObjectClass, kind) || pObject->name == nullptr || findObject(pObject->name) != nullptr) {
            return -1;
        }
        ObjectEntry entry;
        entry.mName = copyText(pObject->name);
        entry.mArchive = copyText(pObject->archive);
        entry.mClass = NebulaObjectClass();
        memcpy(&entry.mClass, pObject, pObject->size < sizeof(NebulaObjectClass) ? pObject->size : sizeof(NebulaObjectClass));
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
        // The tables keep both as a byte.
        if (pGalaxy->stars_to_open < 0 || pGalaxy->stars_to_open > 255 || pGalaxy->dome < 0 || pGalaxy->dome > 255) {
            return -1;
        }
        // A galaxy is its scenario file: without one (a mod that ships none, or files that could not be cooked)
        // it would be a name with no stars, scenarios or stage, so it never enters a table.
        MR::FormattedName scenarioFile;
        MR::makeScenarioArchiveFileName(scenarioFile, pGalaxy->name);
        if (GameAssets::find(scenarioFile.c_str()) < 0) {
            return -2;
        }
        NebulaDataTables::AddedGalaxy added = {};
        added.mName = copyText(pGalaxy->name);
        added.mPane = copyText(pGalaxy->map_pane);
        if (pGalaxy->has_map_position != 0) {
            MapSpot spot = {added.mName, pGalaxy->map_position[0], pGalaxy->map_position[1], pGalaxy->dome};
            sMapSpots.add(spot);
            // A layout's pane names are short: this one is only ever looked for by the game.
            if (added.mPane == nullptr) {
                MR::FormattedName paneName;
                paneName.format("NebMap%d", sMapSpots.mCount);
                added.mPane = copyText(paneName.c_str());
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
            if (MR::isEqualStringPrefix(pName, pPrefix)) {
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
        const char* const pSlash = strrchr(pFile, '/');
        if (pEnd != nullptr && pSlash != nullptr && pEnd < pSlash) {
            pEnd = nullptr;  // a dot in a folder name
        }
        const size_t stem = pEnd != nullptr ? static_cast< size_t >(pEnd - pFile) : strlen(pFile);
        const bool rooted = pFile[0] == '/';
        char* pPath = new (NebulaArena::root(), 0) char[stem + 6];
        pPath[0] = '/';
        memcpy(pPath + 1, pFile + (rooted ? 1 : 0), stem - (rooted ? 1 : 0));
        memcpy(pPath + 1 + stem - (rooted ? 1 : 0), ".ast", 5);
        for (int i = 0; i < sTracks.mCount; i++) {
            if (MR::isEqualString(sTracks.mItems[i].mName, pName)) {
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
        if (pName == nullptr || pName[0] == '\0' || kind < 0 || kind > 255 || a < 0 || a > 65535 || b < 0 || b > 65535) {
            return -1;
        }
        GameEventFlagTable::setFlag(copyText(pName), static_cast< u8 >(kind), static_cast< u16 >(a), static_cast< u16 >(b), copyText(pFirst), copyText(pSecond));
        return 0;
    }

    struct ObjectChange {
        s32 mWhere;
        const char* mObject;
        const char* mBecomes;
    };
    List< ObjectChange > sObjectChanges;

    int32_t apiSetObjectChange(int32_t where, const char* pObject, const char* pBecomes) {
        if (where < 0 || where >= NEBULA_OBJECTS_IN_COUNT || pObject == nullptr || pObject[0] == '\0') {
            return -1;
        }
        const ObjectChange change = {where, copyText(pObject), pBecomes != nullptr ? copyText(pBecomes) : nullptr};
        sObjectChanges.add(change);
        return 0;
    }

    struct CometLimit {
        const char* mGalaxy;
        s32 mMission;
        u32 mSeconds;
    };
    List< CometLimit > sCometLimits;

    int32_t apiSetCometTimeLimit(const char* pGalaxy, int32_t mission, int32_t seconds) {
        if (pGalaxy == nullptr || pGalaxy[0] == '\0' || mission <= 0 || seconds <= 0) {
            return -1;
        }
        const CometLimit limit = {copyText(pGalaxy), mission, static_cast< u32 >(seconds)};
        sCometLimits.add(limit);
        return 0;
    }

    int32_t apiSetCometSchedule(int32_t dome, const char* const* ppGalaxies, const int32_t* pCoins, int32_t count) {
        if (dome < 1 || ppGalaxies == nullptr || pCoins == nullptr || count <= 0) {
            return -1;
        }
        GalaxyCometTimePaper* pPaper = new (NebulaArena::root(), 0) GalaxyCometTimePaper[static_cast< size_t >(count) + 1];
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
        MR::FormattedName path(pFile[0] == '/' ? "%s" : "/%s", pFile);
        if (GameAssets::find(path.c_str()) < 0) {
            return 0;
        }
        if (!MR::isExistSceneObj(SceneObj_MoviePlayerSimple)) {
            return 0;
        }
        MR::startMoviePlayer(copyText(path.c_str()));
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

    // UTF-8 as the game's UTF-16, kept as copyText keeps text.
    const char16_t* wordsOf(const char* pUtf8, NebulaArena* pArena = NebulaArena::root()) {
        const size_t length = strlen(pUtf8);
        char16_t* pWords = new (pArena, 0) char16_t[length + 1];
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
        return pWords;
    }

    int32_t apiAddText(const char* pId, const char* pUtf8) {
        if (pId == nullptr || pUtf8 == nullptr) {
            return -1;
        }
        ModText text = {copyText(pId), wordsOf(pUtf8, NebulaArena::root())};
        sTexts.add(text);
        return 0;
    }

    const char* sFirstStage = nullptr;
    s32 sFirstMission = 1;
    const char* sHubStage = nullptr;
    bool sNoStoryScenes = false;

    int32_t apiSetStory(const char* pKey, const char* pValue) {
        if (pKey == nullptr || pValue == nullptr) {
            return -1;
        }
        if (MR::isEqualString(pKey, "first_stage")) {
            sFirstStage = copyText(pValue);
        } else if (MR::isEqualString(pKey, "first_mission")) {
            s32 mission = 0;
            for (const char* pAt = pValue; *pAt >= '0' && *pAt <= '9'; pAt++) {
                mission = mission * 10 + (*pAt - '0');
            }
            sFirstMission = mission > 0 ? mission : 1;
        } else if (MR::isEqualString(pKey, "hub")) {
            sHubStage = copyText(pValue);
        } else if (MR::isEqualString(pKey, "story_scenes")) {
            sNoStoryScenes = MR::isEqualString(pValue, "off");
        } else {
            return -1;
        }
        return 0;
    }

    void apiSaveGame() {
        if (sStageFrames > 0) {
            GameSequenceFunction::startGameDataSaveSequence(false, false);
        }
    }

    // A galaxy with scenario data (the game's or a mod's): one a stage change or the scenario select can open.
    bool isGalaxy(const char* pGalaxy) {
        return pGalaxy != nullptr && ScenarioDataFunction::getScenarioDataParser()->getScenarioData(pGalaxy) != nullptr;
    }

    void apiOpenScenarioSelect(const char* pGalaxy) {
        if (isGalaxy(pGalaxy) && sStageFrames > 0) {
            MR::requestStartScenarioSelect(copyText(pGalaxy));
        }
    }

    int32_t apiSettingByNumber(uint32_t id, int32_t missing) {
        u32 value = static_cast< u32 >(missing);
        NebulaHost::codePatch(id, &value);
        return static_cast< s32 >(value);
    }

    int32_t apiAddPowerUp(const NebulaPowerUpClass* pClass) {
        if (pClass == nullptr || pClass->size < offsetof(NebulaPowerUpClass, model)) {
            return -1;
        }
        NebulaPowerUpClass power = NebulaPowerUpClass();
        memcpy(&power, pClass, pClass->size < sizeof(NebulaPowerUpClass) ? pClass->size : sizeof(NebulaPowerUpClass));
        power.name = copyText(pClass->name);
        power.music = copyText(pClass->music);
        power.model = copyText(power.model);
        sPowerUps.add(power);
        return NebulaMods::cFirstPowerUp + sPowerUps.mCount - 1;
    }

    const NebulaPowerUpClass* powerUpOf(u16 mode) {
        const int index = static_cast< int >(mode) - NebulaMods::cFirstPowerUp;
        return index >= 0 && index < sPowerUps.mCount ? &sPowerUps.mItems[index] : nullptr;
    }

    // The player in a stage being played: none between scenes or on the file select screen, whose player has no power-up models.
    MarioActor* playingPlayer() {
        if (sStageFrames <= 0 || MR::isStageFileSelect() || !MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return nullptr;
        }
        return MarioAccess::getPlayerActor();
    }

    NebulaActor* apiPlayer() {
        return asHandle(playingPlayer());
    }

    void apiGetPosition(NebulaActor* pActor, f32* pOut) {
        const Vec3f& rAt = asActor(pActor)->mPosition;
        pOut[0] = rAt.x;
        pOut[1] = rAt.y;
        pOut[2] = rAt.z;
    }

    void apiGetGravity(NebulaActor* pActor, f32* pOut) {
        const Vec3f& rDown = asActor(pActor)->mGravity;
        pOut[0] = rDown.x;
        pOut[1] = rDown.y;
        pOut[2] = rDown.z;
    }

    void apiSetPosition(NebulaActor* pActor, const f32* pIn) {
        const Vec3f at(pIn[0], pIn[1], pIn[2]);
        if (pActor == apiPlayer()) {
            MR::setPlayerPos(at);
        } else {
            asActor(pActor)->mPosition.set(at);
        }
    }

    void apiGetVelocity(NebulaActor* pActor, f32* pOut) {
        const Vec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pOut[0] = pVelocity->x;
        pOut[1] = pVelocity->y;
        pOut[2] = pVelocity->z;
    }

    void apiSetVelocity(NebulaActor* pActor, const f32* pIn) {
        Vec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pVelocity->set(pIn[0], pIn[1], pIn[2]);
    }

    int32_t apiValue(NebulaModValue what) {
        MarioActor* pPlayer = playingPlayer();
        switch (what) {
        case NEBULA_VALUE_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mHealth) : 0;
        case NEBULA_VALUE_MAX_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mMaxHealth) : 0;
        case NEBULA_VALUE_POWER_UP: return pPlayer != nullptr ? pPlayer->mPlayerMode : 0;
        case NEBULA_VALUE_ON_GROUND: return pPlayer != nullptr && MR::isOnGroundPlayer() ? 1 : 0;
        case NEBULA_VALUE_STAR_BITS: return MR::getStarPieceNum();
        case NEBULA_VALUE_COINS: return sStageFrames > 0 ? MR::getCoinNum() : 0;
        case NEBULA_VALUE_LIVES: return MR::getPlayerLeft();
        case NEBULA_VALUE_POWER_STARS: return MR::getPowerStarNum();
        case NEBULA_VALUE_WATER_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mWaterLife) : 0;
        case NEBULA_VALUE_SPINNING: return pPlayer != nullptr && MR::isPlayerSwingAction() ? 1 : 0;
        case NEBULA_VALUE_JUMP_RISING: return pPlayer != nullptr && MR::isPlayerJumpRising() ? 1 : 0;
        case NEBULA_VALUE_DAMAGED: return pPlayer != nullptr && MR::isPlayerDamaging() ? 1 : 0;
        case NEBULA_VALUE_DEAD: return pPlayer != nullptr && MR::isPlayerDead() ? 1 : 0;
        case NEBULA_VALUE_GROUND_POUNDING: return pPlayer != nullptr && (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) ? 1 : 0;
        case NEBULA_VALUE_SKATING: return pPlayer != nullptr && MR::isPlayerSkating() ? 1 : 0;
        case NEBULA_VALUE_CROUCHING: return pPlayer != nullptr && MR::isPlayerSquat() ? 1 : 0;
        case NEBULA_VALUE_FLYING: return pPlayer != nullptr && MR::isPlayerFlying() ? 1 : 0;
        case NEBULA_VALUE_SWIMMING: return pPlayer != nullptr && pPlayer->getMario()->isSwimming() ? 1 : 0;
        case NEBULA_VALUE_LUIGI: return MR::isPlayerLuigi() ? 1 : 0;
        case NEBULA_VALUE_MISSION: return sStageFrames > 0 ? MR::getCurrentScenarioNo() : 0;
        default: return 0;
        }
    }

    void apiSetValue(NebulaModSet what, int32_t value) {
        MarioActor* pPlayer = playingPlayer();
        if (pPlayer == nullptr) {
            return;
        }
        switch (what) {
        case NEBULA_SET_LIFE:
            if (value <= 0) {
                pPlayer->mHealth = 0;
                MarioAccess::forceKill(0, 0);
            } else {
                pPlayer->mHealth = static_cast< u32 >(value > static_cast< s32 >(pPlayer->mMaxHealth) ? static_cast< s32 >(pPlayer->mMaxHealth) : value);
            }
            break;
        case NEBULA_SET_MAX_LIFE:
            if (value >= 1) {
                pPlayer->mMaxHealth = static_cast< u32 >(value);
            }
            break;
        case NEBULA_SET_POWER_UP:
            // The game's own numbers, then what add_power_up gave out.
            if (((value >= 0 && value <= 7) || value == 9 || (value >= NebulaMods::cFirstPowerUp && value < NebulaMods::cFirstPowerUp + sPowerUps.mCount)) && pPlayer->canTakePowerUp(value)) {
                pPlayer->setPlayerMode(static_cast< u16 >(value), false);
            }
            break;
        case NEBULA_SET_ADD_COINS:
            if (value > 0) {
                MR::incCoin(value);
            }
            break;
        case NEBULA_SET_ADD_STAR_BITS:
            if (value > 0) {
                MR::addStarPiece(value);
            }
            break;
        case NEBULA_SET_ADD_LIVES:
            // The count stops at the lives setting: a huge number from a mod ends there, not after that many calls.
            for (s32 i = 0; i < value; i++) {
                const s32 before = MR::getPlayerLeft();
                MR::incPlayerLeft();
                if (MR::getPlayerLeft() == before) {
                    break;
                }
            }
            break;
        default: break;
        }
    }

    void apiKillPlayer() {
        if (playingPlayer() != nullptr) {
            MarioAccess::forceKill(0, 0);
        }
    }

    void apiJumpPlayer(const f32* pVelocity) {
        if (playingPlayer() != nullptr) {
            MarioAccess::forceJump(Vec3f(pVelocity[0], pVelocity[1], pVelocity[2]), 0);
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
        case NEBULA_PLAY_SKELETON:
            if (pLive->mModelManager == nullptr || !MR::isExistBck(pLive, pName)) {
                return 0;
            }
            MR::startBck(pLive, pName);
            return 1;
        case NEBULA_PLAY_TEXTURE_MOVE:
            if (pLive->mModelManager == nullptr || !MR::isExistBtk(pLive, pName)) {
                return 0;
            }
            MR::startBtk(pLive, pName);
            return 1;
        case NEBULA_PLAY_COLOUR:
            if (pLive->mModelManager == nullptr || !MR::isExistBrk(pLive, pName)) {
                return 0;
            }
            MR::startBrk(pLive, pName);
            return 1;
        case NEBULA_PLAY_SHOWN_PARTS:
            if (pLive->mModelManager == nullptr || !MR::isExistBva(pLive, pName)) {
                return 0;
            }
            MR::startBva(pLive, pName);
            return 1;
        case NEBULA_PLAY_TEXTURE_FRAMES:
            if (pLive->mModelManager == nullptr || !MR::isExistBtp(pLive, pName)) {
                return 0;
            }
            MR::startBtp(pLive, pName);
            return 1;
        case NEBULA_PLAY_MATERIAL_COLOUR:
            if (pLive->mModelManager == nullptr || !MR::isExistBpk(pLive, pName)) {
                return 0;
            }
            MR::startBpk(pLive, pName);
            return 1;
        case NEBULA_PLAY_PLAYER_ANIMATION:
            if (playingPlayer() == nullptr) {
                return 0;
            }
            playingPlayer()->changeAnimationNonStop(pName);
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

    // Objects a plugin can't make: not actors, or they need what only their level holds (a rail, a talk table, a file
    // of that stage).
    const char* const cNotActors[] = {
#include "NebulaNotActors.inc"
    };
    const char* const cNeedTheirLevel[] = {
#include "NebulaNeedTheirLevel.inc"
    };

    template < int N >
    bool isListed(const char* const (&rList)[N], const char* pName) {
        for (int i = 0; i < N; i++) {
            if (MR::isEqualString(rList[i], pName)) {
                return true;
            }
        }
        return false;
    }

    NebulaActor* apiSpawn(const char* pName, const f32* pAt) {
        if (pName == nullptr || sStageFrames <= 0) {
            return nullptr;
        }
        if (findObject(pName) == nullptr && (isListed(cNotActors, pName) || isListed(cNeedTheirLevel, pName))) {
            return nullptr;
        }
        // Any object, a plugin's or the game's, on any stage: its files load now if the stage hasn't them.
        const ObjectEntry* pEntry = findObject(pName);
        NameObj* pObject = nullptr;
        struct InZone {
            InZone() {
                MR::setCurrentPlacementZoneId(0);
            }
            ~InZone() {
                MR::clearCurrentPlacementZoneId();
            }
        } inZone;
        if (pEntry != nullptr) {
            if (pEntry->mArchive != nullptr && !MR::isExistObjectOrLayoutArchive(pEntry->mArchive)) {
                return nullptr;
            }
            pObject = pEntry->mCreator(pEntry->mName);
        } else {
            const CreatorFuncPtr pCreator = NameObjFactory::getCreator(pName);
            if (pCreator == nullptr) {
                return nullptr;
            }
            pObject = pCreator(copyText(pName, NebulaArena::current()));
        }
        if (pObject == nullptr) {
            return nullptr;
        }
        LiveActor* pActor = static_cast< LiveActor* >(pObject);
        pActor->mPosition.set(pAt[0], pAt[1], pAt[2]);
        pActor->initWithoutIter();
        return asHandle(pActor);
    }

    // The swing is the player's state, not a button: this frame's and the last one's, kept by the frame tick.
    bool sSwingNow = false;
    bool sSwingBefore = false;

    uint32_t apiButtonsHeld() {
        return (MR::getPlayerHeldJump() ? NEBULA_BUTTON_JUMP : 0u) | (MR::getPlayerHeldFire() ? NEBULA_BUTTON_FIRE : 0u) | (sSwingNow ? NEBULA_BUTTON_SPIN : 0u) |
               (MR::getPlayerHeldCrouch() ? NEBULA_BUTTON_CROUCH : 0u) | (MR::getPlayerHeldCamera() ? NEBULA_BUTTON_CAMERA : 0u) |
               (MR::isHeldUp(0) ? NEBULA_BUTTON_DPAD_UP : 0u) | (MR::isHeldDown(0) ? NEBULA_BUTTON_DPAD_DOWN : 0u) | (MR::isHeldLeft(0) ? NEBULA_BUTTON_DPAD_LEFT : 0u) |
               (MR::isHeldRight(0) ? NEBULA_BUTTON_DPAD_RIGHT : 0u) | (MR::isHeldPause(0) ? NEBULA_BUTTON_PAUSE : 0u) | (MR::isHeldMap(0) ? NEBULA_BUTTON_MAP : 0u);
    }

    uint32_t apiButtonsPressed() {
        return (MR::getPlayerPressedJump() ? NEBULA_BUTTON_JUMP : 0u) | (MR::getPlayerPressedFire() ? NEBULA_BUTTON_FIRE : 0u) | (sSwingNow && !sSwingBefore ? NEBULA_BUTTON_SPIN : 0u) |
               (MR::getPlayerPressedCrouch() ? NEBULA_BUTTON_CROUCH : 0u) | (MR::getPlayerPressedCamera() ? NEBULA_BUTTON_CAMERA : 0u) |
               (MR::isPressedUp(0) ? NEBULA_BUTTON_DPAD_UP : 0u) | (MR::isPressedDown(0) ? NEBULA_BUTTON_DPAD_DOWN : 0u) | (MR::isPressedLeft(0) ? NEBULA_BUTTON_DPAD_LEFT : 0u) |
               (MR::isPressedRight(0) ? NEBULA_BUTTON_DPAD_RIGHT : 0u) | (MR::isPressedPause(0) ? NEBULA_BUTTON_PAUSE : 0u) | (MR::isPressedMap(0) ? NEBULA_BUTTON_MAP : 0u);
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

    int32_t apiIsAnimationDone(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager == nullptr || MR::isBckStopped(pLive) ? 1 : 0;
    }

    float apiAnimationFrame(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager != nullptr ? MR::getBckFrame(pLive) : 0.0f;
    }

    float apiAnimationLength(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager != nullptr ? MR::getBckFrameMax(pLive) : 0.0f;
    }

    void apiSetAnimationFrame(NebulaActor* pActor, float frame) {
        const LiveActor* pLive = asActor(pActor);
        if (pLive->mModelManager != nullptr) {
            MR::setBckFrame(pLive, frame);
        }
    }

    void apiSetAnimationRate(NebulaActor* pActor, float rate) {
        const LiveActor* pLive = asActor(pActor);
        if (pLive->mModelManager != nullptr) {
            MR::setBckRate(pLive, rate);
        }
    }

    void apiStopEffect(NebulaActor* pActor, const char* pName) {
        LiveActor* pLive = asActor(pActor);
        if (pLive->mEffectKeeper == nullptr) {
            return;
        }
        if (pName == nullptr) {
            MR::deleteEffectAll(pLive);
        } else {
            MR::deleteEffect(pLive, pName);
        }
    }

    void apiStopSound(NebulaActor* pActor, const char* pName) {
        const LiveActor* pLive = asActor(pActor);
        if (pName != nullptr && pLive->mSoundObject != nullptr) {
            MR::stopSound(pLive, pName);
        }
    }

    void apiSetState(NebulaActor* pActor, int32_t state) {
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            pPlugin->mState = state;
            pPlugin->mStateFrames = -1;
        }
    }

    int32_t apiState(NebulaActor* pActor) {
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mState : 0;
    }

    int32_t apiStateFrames(NebulaActor* pActor) {
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mStateFrames : 0;
    }

    void apiGetRotation(NebulaActor* pActor, float* pOut) {
        const Vec3f& rIn = asActor(pActor)->mRotation;
        pOut[0] = rIn.x;
        pOut[1] = rIn.y;
        pOut[2] = rIn.z;
    }

    void apiSetRotation(NebulaActor* pActor, const float* pIn) {
        asActor(pActor)->mRotation.set(pIn[0], pIn[1], pIn[2]);
    }

    void apiGetScale(NebulaActor* pActor, float* pOut) {
        const Vec3f& rIn = asActor(pActor)->mScale;
        pOut[0] = rIn.x;
        pOut[1] = rIn.y;
        pOut[2] = rIn.z;
    }

    void apiSetScale(NebulaActor* pActor, const float* pIn) {
        asActor(pActor)->mScale.set(pIn[0], pIn[1], pIn[2]);
    }

    void apiSetFront(NebulaActor* pActor, const float* pIn) {
        PluginActor* pPlugin = asPluginActor(pActor);
        Vec3f front(pIn[0], pIn[1], pIn[2]);
        if (pPlugin != nullptr && !MR::isNearZero(front)) {
            MR::normalize(&front);
            pPlugin->mFront = front;
            pPlugin->mUseFront = true;
        }
    }

    void apiGetFront(NebulaActor* pActor, float* pOut) {
        const PluginActor* pPlugin = asPluginActor(pActor);
        pOut[0] = pPlugin != nullptr ? pPlugin->mFront.x : 0.0f;
        pOut[1] = pPlugin != nullptr ? pPlugin->mFront.y : 0.0f;
        pOut[2] = pPlugin != nullptr ? pPlugin->mFront.z : 1.0f;
    }

    void apiTurnToPlayer(NebulaActor* pActor, float degrees) {
        PluginActor* pPlugin = asPluginActor(pActor);
        if (pPlugin != nullptr && playingPlayer() != nullptr) {
            MR::turnDirectionToPlayerDegree(pPlugin, &pPlugin->mFront, degrees);
            pPlugin->mUseFront = true;
        }
    }

    void apiSetTouchable(NebulaActor* pActor, int32_t touchable) {
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            if (touchable != 0) {
                MR::validateHitSensors(pPlugin);
            } else {
                MR::invalidateHitSensors(pPlugin);
            }
        }
    }

    int32_t apiHitPlayer(NebulaActor* pActor, NebulaHit how) {
        PluginActor* pPlugin = asPluginActor(pActor);
        MarioActor* pPlayer = playingPlayer();
        if (pPlugin == nullptr || pPlayer == nullptr) {
            return 0;
        }
        HitSensor* pOwn = pPlugin->getSensor("body");
        HitSensor* pThem = pPlayer->getSensor("body");
        if (pOwn == nullptr || pThem == nullptr) {
            return 0;
        }
        switch (how) {
        case NEBULA_HIT_STRONG: return MR::sendMsgEnemyAttackStrong(pThem, pOwn) ? 1 : 0;
        case NEBULA_HIT_FIRE: return MR::sendMsgEnemyAttackFire(pThem, pOwn) ? 1 : 0;
        case NEBULA_HIT_ELECTRIC: return MR::sendMsgEnemyAttackElectric(pThem, pOwn) ? 1 : 0;
        case NEBULA_HIT_FREEZE: return MR::sendMsgEnemyAttackFreeze(pThem, pOwn) ? 1 : 0;
        case NEBULA_HIT_FLIP: return MR::sendMsgEnemyAttackFlip(pThem, pOwn) ? 1 : 0;
        default: return MR::sendMsgEnemyAttack(pThem, pOwn) ? 1 : 0;
        }
    }

    int32_t apiObjectArg(NebulaActor* pActor, int32_t index) {
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr && index >= 0 && index < 8 ? pPlugin->mArgs[index] : -1;
    }

    int32_t apiHasSwitch(NebulaActor* pActor, NebulaSwitch which) {
        const LiveActor* pLive = asActor(pActor);
        if (pLive->mStageSwitchCtrl == nullptr) {
            return 0;
        }
        switch (which) {
        case NEBULA_SWITCH_A: return MR::isValidSwitchA(pLive) ? 1 : 0;
        case NEBULA_SWITCH_B: return MR::isValidSwitchB(pLive) ? 1 : 0;
        case NEBULA_SWITCH_APPEAR: return MR::isValidSwitchAppear(pLive) ? 1 : 0;
        case NEBULA_SWITCH_DEAD: return MR::isValidSwitchDead(pLive) ? 1 : 0;
        default: return 0;
        }
    }

    int32_t apiIsSwitchOn(NebulaActor* pActor, NebulaSwitch which) {
        const LiveActor* pLive = asActor(pActor);
        if (apiHasSwitch(pActor, which) == 0) {
            return 0;
        }
        switch (which) {
        case NEBULA_SWITCH_A: return MR::isOnSwitchA(pLive) ? 1 : 0;
        case NEBULA_SWITCH_B: return MR::isOnSwitchB(pLive) ? 1 : 0;
        case NEBULA_SWITCH_APPEAR: return MR::isOnSwitchAppear(pLive) ? 1 : 0;
        default: return 0;
        }
    }

    void apiSetSwitch(NebulaActor* pActor, NebulaSwitch which, int32_t on) {
        LiveActor* pLive = asActor(pActor);
        if (apiHasSwitch(pActor, which) == 0) {
            return;
        }
        switch (which) {
        case NEBULA_SWITCH_A:
            if (on != 0) {
                MR::onSwitchA(pLive);
            } else {
                MR::offSwitchA(pLive);
            }
            break;
        case NEBULA_SWITCH_B:
            if (on != 0) {
                MR::onSwitchB(pLive);
            } else {
                MR::offSwitchB(pLive);
            }
            break;
        case NEBULA_SWITCH_DEAD:
            if (on != 0) {
                MR::onSwitchDead(pLive);
            }
            break;
        default: break;
        }
    }

    int32_t apiHasRail(NebulaActor* pActor) {
        return MR::isExistRail(asActor(pActor)) ? 1 : 0;
    }

    void apiMoveOnRail(NebulaActor* pActor, float speed) {
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::moveCoord(pLive, speed);
            MR::moveTransToCurrentRailPos(pLive);
        }
    }

    int32_t apiRailAtEnd(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) && MR::isRailReachedGoal(pLive) ? 1 : 0;
    }

    void apiRailTurnBack(NebulaActor* pActor) {
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::reverseRailDirection(pLive);
        }
    }

    float apiRailCoord(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) ? MR::getRailCoord(pLive) : 0.0f;
    }

    void apiSetRailCoord(NebulaActor* pActor, float coord) {
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::setRailCoord(pLive, coord);
        }
    }

    float apiRailLength(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) ? MR::getRailTotalLength(pLive) : 0.0f;
    }

    int32_t apiRaycast(const float* pFrom, const float* pTo, float* pHit) {
        if (sStageFrames <= 0) {
            return 0;
        }
        const Vec3f from(pFrom[0], pFrom[1], pFrom[2]);
        const Vec3f line(pTo[0] - pFrom[0], pTo[1] - pFrom[1], pTo[2] - pFrom[2]);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Triangle triangle;
        if (!MR::getFirstPolyOnLineToMap(&hit, &triangle, from, line)) {
            return 0;
        }
        pHit[0] = hit.x;
        pHit[1] = hit.y;
        pHit[2] = hit.z;
        return 1;
    }

    int32_t apiIsOnGround(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        if (pLive == playingPlayer()) {
            return MR::isOnGroundPlayer() ? 1 : 0;
        }
        return pLive->mBinder != nullptr && MR::isBindedGround(pLive) ? 1 : 0;
    }

    int32_t apiIsTouchingWall(NebulaActor* pActor) {
        const LiveActor* pLive = asActor(pActor);
        return pLive->mBinder != nullptr && MR::isBindedWall(pLive) ? 1 : 0;
    }

    int32_t apiIsInWater(const float* pAt) {
        return sStageFrames > 0 && MR::isInWater(Vec3f(pAt[0], pAt[1], pAt[2])) ? 1 : 0;
    }

    float apiDistanceToPlayer(NebulaActor* pActor) {
        return playingPlayer() != nullptr ? MR::calcDistanceToPlayer(asActor(pActor)) : 0.0f;
    }

    void apiGetJointPosition(NebulaActor* pActor, const char* pJoint, float* pOut) {
        const LiveActor* pLive = asActor(pActor);
        Vec3f at(pLive->mPosition);
        if (pLive->mModelManager != nullptr && pJoint != nullptr) {
            MR::copyJointPos(pLive, pJoint, &at);
        }
        pOut[0] = at.x;
        pOut[1] = at.y;
        pOut[2] = at.z;
    }

    void apiPlayerFront(float* pOut) {
        Vec3f front(0.0f, 0.0f, 1.0f);
        if (playingPlayer() != nullptr) {
            MR::getPlayerFrontVec(&front);
        }
        pOut[0] = front.x;
        pOut[1] = front.y;
        pOut[2] = front.z;
    }

    void apiSetPlayerControl(int32_t on) {
        if (playingPlayer() == nullptr) {
            return;
        }
        if (on != 0) {
            MR::onPlayerControl(true);
        } else {
            MR::offPlayerControl();
        }
    }

    void apiDeclareItem(NebulaActor* pActor, NebulaItem what, int32_t count) {
        const LiveActor* pLive = asActor(pActor);
        switch (what) {
        case NEBULA_ITEM_COINS: MR::declareCoin(pLive, count); break;
        case NEBULA_ITEM_STAR_BITS: MR::declareStarPiece(pLive, count); break;
        case NEBULA_ITEM_POWER_STAR:
            if (count < 0) {
                MR::declarePowerStar(pLive);
            } else {
                MR::declarePowerStar(pLive, count);
            }
            break;
        default: break;
        }
    }

    void apiAppearItem(NebulaActor* pActor, NebulaItem what, const float* pAt, int32_t count) {
        const LiveActor* pLive = asActor(pActor);
        const Vec3f at(pAt[0], pAt[1], pAt[2]);
        switch (what) {
        case NEBULA_ITEM_COINS: MR::appearCoinPop(pLive, at, count); break;
        case NEBULA_ITEM_STAR_BITS: MR::appearStarPiece(pLive, at, count, 10.0f, 40.0f, false); break;
        case NEBULA_ITEM_POWER_STAR:
            if (count < 0) {
                MR::requestAppearPowerStar(pLive, at);
            } else {
                MR::requestAppearPowerStar(pLive, count, at);
            }
            break;
        default: break;
        }
    }

    int32_t apiStartCutscene(NebulaActor* pActor, const char* pName) {
        return pName != nullptr && sStageFrames > 0 && MR::tryStartDemo(asActor(pActor), pName) ? 1 : 0;
    }

    void apiEndCutscene(NebulaActor* pActor, const char* pName) {
        if (pName != nullptr && sStageFrames > 0) {
            MR::endDemo(asActor(pActor), pName);
        }
    }

    int32_t apiIsCutscenePlaying() {
        return sStageFrames > 0 && MR::isDemoActive() ? 1 : 0;
    }

    void apiShakeCameraBy(NebulaShake how) {
        if (sStageFrames <= 0) {
            return;
        }
        switch (how) {
        case NEBULA_SHAKE_WEAK: MR::shakeCameraWeak(); break;
        case NEBULA_SHAKE_NORMAL_STRONG: MR::shakeCameraNormalStrong(); break;
        case NEBULA_SHAKE_STRONG: MR::shakeCameraStrong(); break;
        case NEBULA_SHAKE_VERY_STRONG: MR::shakeCameraVeryStrong(); break;
        default: MR::shakeCameraNormal(); break;
        }
    }

    void apiStartCamera(const char* pName, int32_t frames) {
        if (pName != nullptr && sStageFrames > 0 && MR::getCameraDirector()->getEventParameter(0, pName) != nullptr) {
            MR::startGlobalEventCameraNoTarget(pName, frames);
        }
    }

    void apiEndCamera(const char* pName, int32_t frames) {
        if (pName != nullptr && sStageFrames > 0 && MR::getCameraDirector()->getEventParameter(0, pName) != nullptr) {
            MR::endGlobalEventCamera(pName, frames, true);
        }
    }

    void apiFadeOut(int32_t frames) {
        if (sStageFrames > 0) {
            MR::closeWipeFade(frames);
        }
    }

    void apiFadeIn(int32_t frames) {
        if (sStageFrames > 0) {
            MR::openWipeFade(frames);
        }
    }

    void apiChangeStage(const char* pGalaxy, int32_t mission) {
        if (isGalaxy(pGalaxy) && sStageFrames > 0) {
            MR::requestChangeStageInGameMoving(copyText(pGalaxy), mission);
        }
    }

    void apiBackToDome() {
        if (sStageFrames > 0) {
            MR::requestChangeStageGoBackAstroDome();
        }
    }

    // A mod's galaxy name: the game's spelling of a galaxy the save keeps stars for, or null.
    const char* starGalaxy(const char* pGalaxy) {
        return pGalaxy != nullptr ? GameDataFunction::findStarGalaxyName(pGalaxy) : nullptr;
    }

    int32_t apiIsEventFlagOn(const char* pName) {
        return pName != nullptr && GameEventFlagTable::isExist(pName) && GameDataFunction::isOnGameEventFlag(pName) ? 1 : 0;
    }

    void apiSetEventFlagOn(const char* pName) {
        if (pName != nullptr && GameEventFlagTable::isExist(pName)) {
            GameDataFunction::onGameEventFlag(pName);
        }
    }

    int32_t apiHasStar(const char* pGalaxy, int32_t star) {
        const char* pName = starGalaxy(pGalaxy);
        return pName != nullptr && star > 0 && MR::isOnGameEventFlagPowerStarSuccess(pName, star) ? 1 : 0;
    }

    void apiShowMessage(const char* pText, int32_t centre) {
        if (pText == nullptr || sStageFrames <= 0) {
            return;
        }
        if (centre != 0) {
            MR::appearInformationMessageCenter(pText, false);
        } else {
            MR::appearInformationMessage(pText, false);
        }
    }

    void apiHideMessage() {
        if (sStageFrames > 0) {
            MR::disappearInformationMessage();
        }
    }

    struct SoundEffect {
        const char* mName;
        u32 mId;
    };
    List< SoundEffect > sSoundEffects;

    int32_t apiAddSoundEffect(const char* pName, const char* pFile) {
        if (pName == nullptr || pFile == nullptr || pName[0] == '\0' || pFile[0] == '\0') {
            return -1;
        }
        // A track under its own name, which the game's sound names can't be: "+SE_OJ_COIN_GET".
        MR::FormattedName trackName("+%s", pName);
        if (apiAddMusic(trackName.c_str(), pFile) != 0) {
            return -1;
        }
        const SoundEffect effect = {copyText(pName), NebulaMods::trackId(trackName.c_str())};
        sSoundEffects.add(effect);
        NebulaMods::sHasSoundEffects = true;
        return 0;
    }

    struct StageRule {
        const char* mStage;
        s32 mScenario;  // 0: all its missions
        const char* mActsAs;  // null: no name
        bool mNotOwn;
    };
    List< StageRule > sStageRules;

    int32_t apiSetStageRules(const char* pStage, int32_t scenario, const char* pActsAs, int32_t ownRules) {
        if (pStage == nullptr || pStage[0] == '\0' || scenario < 0) {
            return -1;
        }
        const StageRule rule = {copyText(pStage), scenario, copyText(pActsAs), ownRules == 0};
        sStageRules.add(rule);
        NebulaMods::sHasStageRules = true;
        return 0;
    }

    int32_t apiSetPlayerConstant(int32_t character, const char* pName, int32_t index, float value) {
        return pName != nullptr && (character == 0 || character == 1) && setMarioConstant(character, pName, index, value) ? 0 : -1;
    }

    float apiPlayerConstant(int32_t character, const char* pName, int32_t index) {
        return pName != nullptr ? getMarioConstant(character, pName, index) : 0.0f;
    }

    struct ObjectClass {
        const char* mName;
        const char* mLike;
    };
    List< ObjectClass > sObjectClasses;

    const ObjectClass* findObjectClass(const char* pName) {
        for (int i = sObjectClasses.mCount - 1; i >= 0; i--) {
            if (MR::isEqualStringCase(sObjectClasses.mItems[i].mName, pName)) {
                return &sObjectClasses.mItems[i];
            }
        }
        return nullptr;
    }

    int32_t apiSetObjectClass(const char* pName, const char* pLike) {
        if (pName == nullptr || pName[0] == '\0' || pLike == nullptr || pLike[0] == '\0' || findObject(pName) != nullptr) {
            return -1;
        }
        const ObjectClass row = {copyText(pName), copyText(pLike)};
        sObjectClasses.add(row);
        return 0;
    }

    // ---- finding and hitting
    bool isActorNamed(const LiveActor* pActor, const char* pName) {
        if (pName == nullptr) {
            return true;
        }
        return MR::isEqualString(pActor->mName, pName) || MR::isEqualString(pActor->mName, MR::getJapaneseObjectName(pName));
    }

    NebulaActor* apiFindActor(const char* pName, NebulaActor* pAfter) {
        if (sStageFrames <= 0) {
            return nullptr;
        }
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        bool found = pAfter == nullptr;
        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pActor = pGroup->getActor(i);
            if (!found) {
                found = asHandle(pActor) == pAfter;
                continue;
            }
            if (isActorNamed(pActor, pName)) {
                return asHandle(pActor);
            }
        }
        return nullptr;
    }

    int32_t apiActorsNear(const float* pAt, float radius, NebulaActor** ppOut, int32_t max) {
        if (sStageFrames <= 0 || pAt == nullptr) {
            return 0;
        }
        const Vec3f at(pAt[0], pAt[1], pAt[2]);
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        s32 count = 0;
        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* pActor = pGroup->getActor(i);
            if (MR::isDead(pActor) || (pActor->mPosition.x - at.x) * (pActor->mPosition.x - at.x) + (pActor->mPosition.y - at.y) * (pActor->mPosition.y - at.y) + (pActor->mPosition.z - at.z) * (pActor->mPosition.z - at.z) > radius * radius) {
                continue;
            }
            if (ppOut != nullptr && count < max) {
                ppOut[count] = asHandle(pActor);
            }
            count++;
        }
        return count;
    }

    int32_t apiAttackActor(NebulaActor* pTarget, NebulaActor* pBy, NebulaAttack how) {
        LiveActor* pLive = asActor(pTarget);
        LiveActor* pFrom = pBy != nullptr ? asActor(pBy) : playingPlayer();
        if (pLive == nullptr || pFrom == nullptr || pLive->mSensorKeeper == nullptr || pFrom->mSensorKeeper == nullptr) {
            return 0;
        }
        HitSensor* pThem = pLive->mSensorKeeper->getNthSensorInfo(0)->mSensor;
        HitSensor* pOwn = pFrom->mSensorKeeper->getNthSensorInfo(0)->mSensor;
        if (pThem == nullptr || pOwn == nullptr) {
            return 0;
        }
        u32 message = ACTMES_PLAYER_PUNCH;
        switch (how) {
        case NEBULA_ATTACK_STOMP: message = ACTMES_PLAYER_TRAMPLE; break;
        case NEBULA_ATTACK_GROUND_POUND: message = ACTMES_PLAYER_HIP_DROP; break;
        case NEBULA_ATTACK_STAR_BIT: message = ACTMES_STAR_PIECE_ATTACK; break;
        case NEBULA_ATTACK_FIRE: message = ACTMES_FIREBALL_ATTACK; break;
        case NEBULA_ATTACK_INVINCIBLE: message = ACTMES_INVINCIBLE_ATTACK; break;
        case NEBULA_ATTACK_SHELL: message = ACTMES_JET_TURTLE_ATTACK; break;
        default: break;
        }
        return MR::sendArbitraryMsg(message, pThem, pOwn) ? 1 : 0;
    }

    void* apiActorData(NebulaActor* pActor) {
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mData : nullptr;
    }

    void apiSetActorData(NebulaActor* pActor, void* pData) {
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            pPlugin->mData = pData;
        }
    }

    const char* apiActorPlacedName(NebulaActor* pActor) {
        return asActor(pActor)->mName;
    }

    void apiStopScene(int32_t frames) {
        if (sStageFrames > 0 && frames > 0) {
            MR::stopScene(frames);
        }
    }

    void apiGravityAt(const float* pAt, float* pOut) {
        Vec3f down(0.0f, -1.0f, 0.0f);
        if (sStageFrames > 0) {
            MR::calcGravityVector(MR::getAllLiveActorGroup(), Vec3f(pAt[0], pAt[1], pAt[2]), &down, nullptr, 0);
        }
        pOut[0] = down.x;
        pOut[1] = down.y;
        pOut[2] = down.z;
    }

    int32_t apiIsInArea(const char* pArea, const float* pAt) {
        return sStageFrames > 0 && pArea != nullptr && MR::getAreaObj(pArea, Vec3f(pAt[0], pAt[1], pAt[2])) != nullptr ? 1 : 0;
    }

    void apiCameraPosition(float* pOut) {
        const Vec3f at = sStageFrames > 0 ? MR::getCamPos() : Vec3f(0.0f, 0.0f, 0.0f);
        pOut[0] = at.x;
        pOut[1] = at.y;
        pOut[2] = at.z;
    }

    void apiCameraTarget(float* pOut) {
        Vec3f at(0.0f, 0.0f, 0.0f);
        if (sStageFrames > 0) {
            const Vec3f from = MR::getCamPos();
            const Vec3f way = MR::getCamZdir();
            at.set(from.x + way.x * 1000.0f, from.y + way.y * 1000.0f, from.z + way.z * 1000.0f);
        }
        pOut[0] = at.x;
        pOut[1] = at.y;
        pOut[2] = at.z;
    }

    int32_t apiScreenPosition(const float* pAt, float* pOut) {
        if (sStageFrames <= 0) {
            return 0;
        }
        Vec2f screen;
        const bool front = MR::calcScreenPosition(&screen, Vec3f(pAt[0], pAt[1], pAt[2]));
        pOut[0] = screen.x;
        pOut[1] = screen.y;
        return front ? 1 : 0;
    }

    void apiPlaySystemSound(const char* pName) {
        if (pName != nullptr) {
            MR::startSystemSE(pName);
        }
    }

    int32_t apiPointer(float* pOut) {
        if (!MR::isStarPointerInScreen(0)) {
            return 0;
        }
        const Vec2f* pAt = MR::getStarPointerScreenPosition(0);
        pOut[0] = pAt->x;
        pOut[1] = pAt->y;
        return 1;
    }

    int32_t apiEventValue(const char* pName) {
        return pName != nullptr ? static_cast< s32 >(GameDataFunction::getGameEventValue(pName)) : 0;
    }

    void apiSetEventValue(const char* pName, int32_t value) {
        if (pName != nullptr) {
            // The saved value is 16 bits: out of range is held at its ends, not wrapped.
            GameDataFunction::setGameEventValue(pName, static_cast< u16 >(value < 0 ? 0 : value > 65535 ? 65535 : value));
        }
    }

    void apiSetStar(const char* pGalaxy, int32_t star, int32_t have) {
        const char* pName = starGalaxy(pGalaxy);
        if (pName != nullptr && star > 0) {
            GameDataFunction::setGameFlagPowerStarSuccess(pName, star, have != 0);
        }
    }

    int32_t apiStarsInGalaxy(const char* pGalaxy) {
        const ScenarioData* pData =
            pGalaxy != nullptr ? ScenarioDataFunction::getScenarioDataParser()->getScenarioData(pGalaxy) : nullptr;
        return pData != nullptr ? pData->getPowerStarNum() : 0;
    }

    int32_t apiStarsWonInGalaxy(const char* pGalaxy) {
        const char* pName = starGalaxy(pGalaxy);
        return pName != nullptr ? GameDataFunction::getPowerStarNumOwned(pName) : 0;
    }

    // ---- layouts
    class PluginLayout : public LayoutActor {
    public:
        PluginLayout(const char* pArchive) : LayoutActor("PluginLayout", true), mArchive(pArchive) {}

        virtual void init(const TableIter&) {
            initLayoutManager(mArchive, 1);
            MR::connectToSceneLayout(this);
            appear();
        }

        const char* mArchive;
        struct Moved {
            const char* mPane;
            Vec2f mAt;
        };
        List< Moved* > mMoved;
    };

    PluginLayout* asLayout(NebulaLayout* pLayout) {
        return reinterpret_cast< PluginLayout* >(pLayout);
    }

    NebulaLayout* apiLayoutOpen(const char* pName) {
        if (pName == nullptr || sStageFrames <= 0) {
            return nullptr;
        }
        if (!MR::isExistObjectOrLayoutArchive(pName)) {
            return nullptr;
        }
        PluginLayout* pLayout = new PluginLayout(copyText(pName, NebulaArena::current()));
        pLayout->initWithoutIter();
        return reinterpret_cast< NebulaLayout* >(pLayout);
    }

    void apiLayoutShow(NebulaLayout* pLayout, int32_t show) {
        if (show != 0) {
            MR::showLayout(asLayout(pLayout));
        } else {
            MR::hideLayout(asLayout(pLayout));
        }
    }

    void apiLayoutSetText(NebulaLayout* pLayout, const char* pPane, const char* pUtf8) {
        if (pPane != nullptr && pUtf8 != nullptr) {
            MR::setTextBoxMessageRecursive(asLayout(pLayout), pPane, wordsOf(pUtf8, NebulaArena::findFromRoot(asLayout(pLayout))));
        }
    }

    void apiLayoutSetNumber(NebulaLayout* pLayout, const char* pPane, int32_t number) {
        if (pPane != nullptr) {
            MR::setTextBoxNumberRecursive(asLayout(pLayout), pPane, number);
        }
    }

    void apiLayoutShowPane(NebulaLayout* pLayout, const char* pPane, int32_t show) {
        if (pPane == nullptr) {
            return;
        }
        if (show != 0) {
            MR::showPaneRecursive(asLayout(pLayout), pPane);
        } else {
            MR::hidePaneRecursive(asLayout(pLayout), pPane);
        }
    }

    void apiLayoutMovePane(NebulaLayout* pLayout, const char* pPane, float x, float y) {
        if (pPane == nullptr) {
            return;
        }
        PluginLayout* pHost = asLayout(pLayout);
        for (int i = 0; i < pHost->mMoved.mCount; i++) {
            if (MR::isEqualString(pHost->mMoved.mItems[i]->mPane, pPane)) {
                pHost->mMoved.mItems[i]->mAt.set(x, y);
                return;
            }
        }
        NebulaArena* const pHostArena = NebulaArena::findFromRoot(pHost);
        PluginLayout::Moved* pMoved = new (pHostArena) PluginLayout::Moved;
        pMoved->mPane = copyText(pPane, pHostArena);
        pMoved->mAt.set(x, y);
        pHost->mMoved.add(pMoved);
        MR::createAndAddPaneCtrl(pHost, pMoved->mPane, 1);
        MR::setFollowPos(&pMoved->mAt, pHost, pMoved->mPane);
        MR::setFollowTypeAdd(pHost, pMoved->mPane);
    }

    void apiLayoutPlay(NebulaLayout* pLayout, const char* pAnimation) {
        if (pAnimation != nullptr) {
            MR::startAnim(asLayout(pLayout), pAnimation, 0);
        }
    }

    int32_t apiLayoutIsPlaying(NebulaLayout* pLayout) {
        return MR::isAnimStopped(asLayout(pLayout), 0) ? 0 : 1;
    }

    void apiLayoutSetAlpha(NebulaLayout* pLayout, float alpha) {
        MR::setLayoutAlphaFloat(asLayout(pLayout), alpha);
    }

    // The fields the host fills are its own: log, option, hook, find_function, store_get, store_set, setting, store_get_text, store_set_text.
    // Set once any mod's rows or code have started.
    bool sAskedForApi = false;

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
        apiSetObjectChange,
        apiSetCometTimeLimit,
        apiIsAnimationDone,
        apiAnimationFrame,
        apiAnimationLength,
        apiSetAnimationFrame,
        apiSetAnimationRate,
        apiStopEffect,
        apiStopSound,
        apiSetState,
        apiState,
        apiStateFrames,
        apiGetRotation,
        apiSetRotation,
        apiGetScale,
        apiSetScale,
        apiSetFront,
        apiGetFront,
        apiTurnToPlayer,
        apiSetTouchable,
        apiHitPlayer,
        apiObjectArg,
        apiHasSwitch,
        apiIsSwitchOn,
        apiSetSwitch,
        apiHasRail,
        apiMoveOnRail,
        apiRailAtEnd,
        apiRailTurnBack,
        apiRailCoord,
        apiSetRailCoord,
        apiRailLength,
        apiRaycast,
        apiIsOnGround,
        apiIsTouchingWall,
        apiIsInWater,
        apiDistanceToPlayer,
        apiGetJointPosition,
        apiPlayerFront,
        apiSetPlayerControl,
        apiDeclareItem,
        apiAppearItem,
        apiStartCutscene,
        apiEndCutscene,
        apiIsCutscenePlaying,
        apiShakeCameraBy,
        apiStartCamera,
        apiEndCamera,
        apiFadeOut,
        apiFadeIn,
        apiChangeStage,
        apiBackToDome,
        apiIsEventFlagOn,
        apiSetEventFlagOn,
        apiHasStar,
        apiShowMessage,
        apiHideMessage,
        nullptr,
        nullptr,
        apiSetStageRules,
        apiSetPlayerConstant,
        apiPlayerConstant,
        apiSetObjectClass,
        apiAddSoundEffect,
        apiFindActor,
        apiActorsNear,
        apiAttackActor,
        apiActorData,
        apiSetActorData,
        apiActorPlacedName,
        apiStopScene,
        apiGravityAt,
        apiIsInArea,
        apiCameraPosition,
        apiCameraTarget,
        apiScreenPosition,
        apiPlaySystemSound,
        apiPointer,
        apiEventValue,
        apiSetEventValue,
        apiSetStar,
        apiStarsInGalaxy,
        apiStarsWonInGalaxy,
        nullptr,
        apiLayoutOpen,
        apiLayoutShow,
        apiLayoutSetText,
        apiLayoutSetNumber,
        apiLayoutShowPane,
        apiLayoutMovePane,
        apiLayoutPlay,
        apiLayoutIsPlaying,
        apiLayoutSetAlpha,
        nullptr,
        nullptr,
        apiSettingByNumber,
        apiOpenScenarioSelect,
        apiSetStory,
        apiSaveGame,
    };
}  // namespace

extern "C" NEBULA_GAME_API const NebulaModApi* nebula_game_mod_api(void) {
    sAskedForApi = true;
    return &sApi;
}

namespace NebulaMods {
    bool hasCode() {
        return sAskedForApi;
    }

    s32 dispatch(NebulaModEvent event, s32 value, void* pActor, const char* pName) {
        const List< Subscriber >& rSubscribers = sSubscribers[event];
        NebulaModEventData data = {value, static_cast< NebulaActor* >(pActor), pName};
        for (int i = 0; i < rSubscribers.mCount; i++) {
            data.value = rSubscribers.mItems[i].mCallback(&data, rSubscribers.mItems[i].mUser);
        }
        return data.value;
    }

    const char* objectBecomes(s32 where, const char* pName) {
        // The last said is the mod highest on the list's.
        for (int i = sObjectChanges.mCount - 1; i >= 0; i--) {
            const ObjectChange& rChange = sObjectChanges.mItems[i];
            if (rChange.mWhere == where && MR::isEqualStringCase(rChange.mObject, pName)) {
                return rChange.mBecomes;
            }
        }
        return pName;
    }

    u32 cometTimeLimit(const char* pGalaxy, s32 mission) {
        for (int i = sCometLimits.mCount - 1; i >= 0; i--) {
            const CometLimit& rLimit = sCometLimits.mItems[i];
            if (rLimit.mMission == mission && MR::isEqualString(rLimit.mGalaxy, pGalaxy)) {
                return NebulaFrameClock::stepsOfSeconds(rLimit.mSeconds);
            }
        }
        return 0;
    }

    StageFrame::StageFrame() {
        sStageFrames++;
        if (sAskedForApi) {
            sSwingBefore = sSwingNow;
            sSwingNow = playingPlayer() != nullptr && MR::isPlayerSwingAction();
            if (sSwingNow && !sSwingBefore) {
                fire(NEBULA_MOD_EVENT_PLAYER_SPIN);
            }
            if (sWanted[NEBULA_MOD_EVENT_PLAYER_JUMP] || sWanted[NEBULA_MOD_EVENT_PLAYER_LAND]) {
                static bool sGroundBefore = true;
                static bool sRisingBefore = false;
                const bool ground = playingPlayer() != nullptr && MR::isOnGroundPlayer();
                const bool rising = playingPlayer() != nullptr && MR::isPlayerJumpRising();
                if (rising && !sRisingBefore) {
                    fire(NEBULA_MOD_EVENT_PLAYER_JUMP);
                }
                if (ground && !sGroundBefore) {
                    fire(NEBULA_MOD_EVENT_PLAYER_LAND);
                }
                sGroundBefore = ground;
                sRisingBefore = rising;
            }
        }
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

    s32 powerUpCount() {
        return sPowerUps.mCount;
    }

    const char* powerUpModel(s32 index) {
        return index >= 0 && index < sPowerUps.mCount ? sPowerUps.mItems[index].model : nullptr;
    }

    u16 powerUpLook(u16 mode) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        return pPower != nullptr ? static_cast< u16 >(pPower->looks_like) : mode;
    }

    const char16_t* text(const char* pId) {
        for (int i = sTexts.mCount - 1; i >= 0; i--) {
            if (MR::isEqualString(sTexts.mItems[i].mId, pId)) {
                return sTexts.mItems[i].mWords;
            }
        }
        return nullptr;
    }

    u32 trackId(const char* pName) {
        for (int i = 0; i < sTracks.mCount; i++) {
            if (MR::isEqualString(sTracks.mItems[i].mName, pName)) {
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
            if (rRow.mScenario < 0 || !MR::isEqualString(rRow.mStage, pStage)) {
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
            if (rRow.mArea == area && MR::isEqualString(rRow.mStage, pStage)) {
                return rRow.mId;
            }
        }
        return static_cast< u32 >(-1);
    }

    bool mapSpot(const char* pGalaxy, f32* pX, f32* pY, s32* pDome) {
        for (int i = sMapSpots.mCount - 1; i >= 0; i--) {
            if (MR::isEqualString(sMapSpots.mItems[i].mGalaxy, pGalaxy)) {
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

    const char* firstStage() {
        return sFirstStage;
    }

    s32 firstMission() {
        return sFirstMission;
    }

    const char* hubStage() {
        return sHubStage;
    }

    bool noStoryScenes() {
        return sNoStoryScenes;
    }

    u32 soundEffectSlow(const char* pName) {
        for (int i = sSoundEffects.mCount - 1; i >= 0; i--) {
            if (MR::isEqualString(sSoundEffects.mItems[i].mName, pName)) {
                return sSoundEffects.mItems[i].mId;
            }
        }
        return static_cast< u32 >(-1);
    }

    s32 stageRuleSlow(const char* pStage, const char* pAskedName) {
        s32 scenario = -1;
        bool notOwn = false;
        for (int i = sStageRules.mCount - 1; i >= 0; i--) {
            const StageRule& rRule = sStageRules.mItems[i];
            if (!MR::isEqualStringCase(rRule.mStage, pStage)) {
                continue;
            }
            if (rRule.mScenario != 0) {
                if (scenario < 0) {
                    scenario = MR::getCurrentScenarioNo();
                }
                if (rRule.mScenario != scenario) {
                    continue;
                }
            }
            if (rRule.mActsAs != nullptr && MR::isEqualStringCase(rRule.mActsAs, pAskedName)) {
                return 1;
            }
            notOwn = notOwn || rRule.mNotOwn;
        }
        return notOwn && MR::isEqualStringCase(pStage, pAskedName) ? 0 : -1;
    }

    bool isMissingFiles(const char* pName) {
        if (!sAskedForApi) {
            return false;
        }
        const ObjectClass* pClass = sObjectClasses.mCount != 0 ? findObjectClass(pName) : nullptr;
        const ObjectEntry* pEntry = sObjects.mCount != 0 ? findObject(pName) : nullptr;
        const char* const archives[] = {pClass != nullptr ? pClass->mName : nullptr, pEntry != nullptr ? pEntry->mArchive : nullptr};
        for (const char* pArchive : archives) {
            if (pArchive != nullptr && !MR::isExistObjectOrLayoutArchive(pArchive)) {
                nebula_log("mods: left out: %s: it has no archive %s\n", pName, pArchive);
                return true;
            }
        }
        return false;
    }

    const char* objectLike(const char* pName) {
        const ObjectClass* pClass = sObjectClasses.mCount != 0 ? findObjectClass(pName) : nullptr;
        return pClass != nullptr ? pClass->mLike : pName;
    }

    const char* modelFor(const char* pObjectName, const char* pModelName) {
        const ObjectClass* pClass = sObjectClasses.mCount != 0 && pObjectName != nullptr ? findObjectClass(pObjectName) : nullptr;
        return pClass != nullptr && MR::isEqualStringCase(pClass->mLike, pModelName) ? pClass->mName : pModelName;
    }

    CreatorFuncPtr findCreator(const char* pName) {
        if (sObjects.mCount == 0) {
            return nullptr;
        }
        const ObjectEntry* pEntry = findObject(pName);
        return pEntry != nullptr ? pEntry->mCreator : nullptr;
    }

}  // namespace NebulaMods
