#include "Game/System/NebulaMods.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaClass.hpp"
#include "Game/System/LumaHost.hpp"
#include "Game/System/LumaRegistry.hpp"
#include "Game/System/LumaScript.hpp"
#include "Game/System/NebulaModCalls.hpp"
#include "nebula_text_tags.h"
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
#include "Game/Util/Tunable.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Animation/XanimeResource.hpp"
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
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
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
#include "Game/Screen/TimeLimitLayout.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
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
        // A game message whose talk (camera, sound, talk and balloon type) a new line takes; nullptr: its own, or none.
        const char* mLike;
    };
    List< ModText > sTexts;
    List< NebulaPowerUpClass > sPowerUps;
    // Each power-up's script (add_form; -1: none) and its instance on the player while he is in it.
    struct FormScript {
        s32 mScript;
        lumascript::Instance* mInstance;
        u32 mGeneration;  // the scene it started in (scriptsSceneGeneration)
    };
    List< FormScript > sFormScripts;
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

    // A plugin's number the game can take: not NaN or infinite, which positions, motion and the camera would carry into
    // collision lookups and the frame. A call given one changes nothing.
    bool isFinite(f32 value) {
        return value - value == 0.0f;
    }

    bool isFinite3(const f32* pValues) {
        return pValues != nullptr && isFinite(pValues[0]) && isFinite(pValues[1]) && isFinite(pValues[2]);
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
            : LiveActor(pName), mClass(pClass), mState(0), mStateFrames(0), mPlacementRow(-1), mUseFront(false),
              mFront(0.0f, 0.0f, 1.0f) {
            sPluginActorTable = *reinterpret_cast< void** >(this);
        }

        virtual void init(const TableIter& rIter) {
            if (rIter.isValid()) {
                MR::initDefaultPos(this, rIter);
                // Its placement row, for placement_number and placement_text: the table's data lives as long as the stage.
                mPlacement.mData = rIter.mInfo->mData;
                mPlacement.mName = rIter.mInfo->mName;
                mPlacementRow = rIter.mIndex;
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

        // Its own handling first (a script's "message"); what it leaves goes the game's way.
        virtual bool receiveMessage(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
            if (mClass->message != nullptr) {
                LiveActor* const pFrom = pSender != nullptr ? MR::getSensorHost(pSender) : nullptr;
                if (mClass->message(asHandle(this), static_cast< int32_t >(msg), pFrom != nullptr ? asHandle(pFrom) : nullptr,
                                    mClass->user) != 0) {
                    return true;
                }
            }
            return LiveActor::receiveMessage(msg, pSender, pReceiver);
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
        Table mPlacement;
        s32 mPlacementRow;
        bool mUseFront;
        Vec3f mFront;
        void* mData = nullptr;
    };

    PluginActor* asPluginActor(NebulaActor* pActor) {
        LiveActor* pLive = asActor(pActor);
        return pLive != nullptr && *reinterpret_cast< void** >(pLive) == sPluginActorTable ? static_cast< PluginActor* >(pLive) : nullptr;
    }

    // Made by its own name, or by a placed object's that a change turns into it (object_changes: the creator is
    // handed the placement's name): either way the actor carries the class's name.
    NameObj* createPluginActor(const char* pName) {
        const ObjectEntry* pEntry = findObject(pName);
        if (pEntry == nullptr) {
            const char* const pBecomes = NebulaMods::objectBecomes(NEBULA_OBJECTS_PLACED, NebulaMods::objectLike(pName));
            pEntry = pBecomes != nullptr ? findObject(pBecomes) : nullptr;
        }
        return pEntry != nullptr ? new PluginActor(pEntry->mName, &pEntry->mClass) : nullptr;
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

    // The mod adding rows now, for the registries: a data mod's scripts name theirs; a plugin's register_object does not
    // say which plugin it is.
    const NebulaPlugin cSomePlugin = {sizeof(NebulaPlugin), "a plugin", ""};
    const NebulaPlugin* sAddingMod = nullptr;

    const NebulaPlugin* addingMod() {
        return sAddingMod != nullptr ? sAddingMod : &cSomePlugin;
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
        lumacore::registryAdd(lumacore::RegistryKind_Object, entry.mName, nullptr, addingMod(), -1, -1);
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

    // UTF-8 as the game's UTF-16 with the game's message tags written in brackets (nebula_text_tags.h), kept as copyText
    // keeps text.
    const char16_t* wordsOf(const char* pUtf8, NebulaArena* pArena = NebulaArena::root()) {
        const u32 units = nebula_text::encode(pUtf8, nullptr, 0);
        char16_t* pWords = new (pArena, 0) char16_t[units + 1];
        nebula_text::encode(pUtf8, pWords, units + 1);
        return pWords;
    }

    int32_t apiAddText(const char* pId, const char* pUtf8) {
        if (pId == nullptr || pUtf8 == nullptr) {
            return -1;
        }
        ModText text = {copyText(pId), wordsOf(pUtf8, NebulaArena::root()), nullptr};
        sTexts.add(text);
        return 0;
    }

    int32_t apiAddTextLike(const char* pId, const char* pUtf8, const char* pLike) {
        if (pId == nullptr || pUtf8 == nullptr) {
            return -1;
        }
        if (apiAddText(pId, pUtf8) != 0) {
            return -1;
        }
        if (pLike != nullptr && pLike[0] != 0) {
            sTexts.mItems[sTexts.mCount - 1].mLike = copyText(pLike);
        }
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

    int32_t apiSetTunable(const char* pKey, double value) {
        return value - value == 0.0 ? nebula::setTunable(pKey, value) : -1;
    }

    int32_t apiGetTunable(const char* pKey, double* pValue, double* pGameValue) {
        return nebula::getTunable(pKey, pValue, pGameValue) ? 0 : -1;
    }

    int32_t apiTunableAt(int32_t index, const char** pFile, const char** pName, int32_t* pPlace, int32_t* pKind, double* pGameValue, double* pValue) {
        u32 kind = 0;
        s32 place = -1;
        const char* pFileOut = nullptr;
        const char* pNameOut = nullptr;
        f64 gameValue = 0.0;
        f64 value = 0.0;
        if (!nebula::tunableAt(index, &pFileOut, &pNameOut, &place, &kind, &gameValue, &value)) {
            return -1;
        }
        if (pFile != nullptr) *pFile = pFileOut;
        if (pName != nullptr) *pName = pNameOut;
        if (pPlace != nullptr) *pPlace = place;
        if (pKind != nullptr) *pKind = static_cast<int32_t>(kind);
        if (pGameValue != nullptr) *pGameValue = gameValue;
        if (pValue != nullptr) *pValue = value;
        return 0;
    }

    // A galaxy with scenario data (the game's or a mod's): one a stage change or the scenario select can open.
    bool isGalaxy(const char* pGalaxy) {
        return pGalaxy != nullptr && ScenarioDataFunction::getScenarioDataParser()->getScenarioData(pGalaxy) != nullptr;
    }

    void apiOpenScenarioSelect(const char* pGalaxy) {
        if (pGalaxy == nullptr) {
            return;
        }
        if (isGalaxy(pGalaxy) && sStageFrames > 0) {
            MR::requestStartScenarioSelect(copyText(pGalaxy));
        }
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
        // Under it, one of the game's forms (an older plugin's class has none: the normal player).
        const s32 acts = power.acts_like;
        if (!((acts >= 0 && acts <= 7) || acts == 9)) {
            power.acts_like = 0;
        }
        sPowerUps.add(power);
        sFormScripts.add(FormScript{-1, nullptr, 0});
        const s32 id = NebulaMods::cFirstPowerUp + sPowerUps.mCount - 1;
        if (power.name != nullptr) {
            lumacore::registryAdd(lumacore::RegistryKind_Form, power.name, nullptr, addingMod(), -1, id);
        }
        return id;
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
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const Vec3f& rAt = asActor(pActor)->mPosition;
        pOut[0] = rAt.x;
        pOut[1] = rAt.y;
        pOut[2] = rAt.z;
    }

    void apiGetGravity(NebulaActor* pActor, f32* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const Vec3f& rDown = asActor(pActor)->mGravity;
        pOut[0] = rDown.x;
        pOut[1] = rDown.y;
        pOut[2] = rDown.z;
    }

    void apiSetPosition(NebulaActor* pActor, const f32* pIn) {
        if (pActor == nullptr || !isFinite3(pIn)) {
            return;
        }
        const Vec3f at(pIn[0], pIn[1], pIn[2]);
        if (pActor == apiPlayer()) {
            MR::setPlayerPos(at);
        } else {
            asActor(pActor)->mPosition.set(at);
        }
    }

    void apiGetVelocity(NebulaActor* pActor, f32* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const Vec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pOut[0] = pVelocity->x;
        pOut[1] = pVelocity->y;
        pOut[2] = pVelocity->z;
    }

    void apiSetVelocity(NebulaActor* pActor, const f32* pIn) {
        if (pActor == nullptr || !isFinite3(pIn)) {
            return;
        }
        Vec3f* pVelocity = pActor == apiPlayer() ? MarioAccess::getVelocity() : &asActor(pActor)->mVelocity;
        pVelocity->set(pIn[0], pIn[1], pIn[2]);
    }

    int32_t apiValue(NebulaModValue what) {
        MarioActor* pPlayer = playingPlayer();
        switch (what) {
        case NEBULA_VALUE_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mHealth) : 0;
        case NEBULA_VALUE_MAX_LIFE: return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mMaxHealth) : 0;
        case NEBULA_VALUE_POWER_UP: return pPlayer != nullptr ? (pPlayer->mModForm != 0 ? pPlayer->mModForm : pPlayer->mPlayerMode) : 0;
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
        if (!isFinite3(pVelocity)) {
            return;
        }
        if (playingPlayer() != nullptr) {
            MarioAccess::forceJump(Vec3f(pVelocity[0], pVelocity[1], pVelocity[2]), 0);
        }
    }

    void apiAppear(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return;
        }
        asActor(pActor)->appear();
    }

    void apiKill(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return;
        }
        asActor(pActor)->kill();
    }

    int32_t apiIsDead(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        return MR::isDead(asActor(pActor)) ? 1 : 0;
    }

    void apiShowModel(NebulaActor* pActor, int32_t show) {
        if (pActor == nullptr) {
            return;
        }
        if (asActor(pActor)->mModelManager == nullptr) {
            return;
        }
        // The player shows his model again each frame (MarioActor::calcAnim) unless a plugin hid it.
        if (asActor(pActor) == MarioAccess::getPlayerActor()) {
            MarioAccess::getPlayerActor()->mModHidden = show == 0;
        }
        if (show != 0) {
            MR::showModel(asActor(pActor));
        } else {
            MR::hideModel(asActor(pActor));
        }
    }

    void apiShowJoint(NebulaActor* pActor, const char* pJoint, int32_t show) {
        if (pActor == nullptr || pJoint == nullptr || asActor(pActor)->mModelManager == nullptr) {
            return;
        }
        if (asActor(pActor) == MarioAccess::getPlayerActor()) {
            MarioAccess::getPlayerActor()->showJointByPlugin(pJoint, show != 0);
            return;
        }
        Model* pModel = MR::getModel(asActor(pActor));
        const s32 idx = pModel->mModelData->findJoint(pJoint);
        if (idx >= 0) {
            MR::showJointAndChildrenOn(pModel, pModel->mModelData->getJointAt(static_cast< u32 >(idx)), show != 0);
        }
    }

    void apiGetJointMatrix(NebulaActor* pActor, const char* pJoint, float* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        for (s32 i = 0; i < 12; i++) {
            pOut[i] = i % 5 == 0 ? 1.0f : 0.0f;
        }
        pOut[3] = pLive->mPosition.x;
        pOut[7] = pLive->mPosition.y;
        pOut[11] = pLive->mPosition.z;
        if (pLive->mModelManager == nullptr || pJoint == nullptr) {
            return;
        }
        Model* pModel = MR::getModel(pLive);
        const s32 idx = pModel->mModelData->findJoint(pJoint);
        if (idx < 0) {
            return;
        }
        MtxPtr mtx = pModel->getAnmMtx(idx);
        for (s32 r = 0; r < 3; r++) {
            for (s32 c = 0; c < 4; c++) {
                pOut[r * 4 + c] = mtx[r][c];
            }
        }
    }

    const char* apiJointName(NebulaActor* pActor, int32_t index) {
        if (pActor == nullptr || index < 0 || asActor(pActor)->mModelManager == nullptr) {
            return nullptr;
        }
        const ModelData* pData = MR::getModel(asActor(pActor))->mModelData;
        if (static_cast< u32 >(index) >= pData->getJointCount()) {
            return nullptr;
        }
        return pData->getJointName(static_cast< u32 >(index));
    }

    int32_t apiPlay(NebulaActor* pActor, NebulaModPlay what, const char* pName) {
        if (pActor == nullptr || pName == nullptr) {
            return 0;
        }
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
            // One of the player's animations (its table's, with the mods'): any other name would play nothing.
            if (playingPlayer() == nullptr || playingPlayer()->mMarioAnim == nullptr ||
                playingPlayer()->mMarioAnim->mResourceTable->getGroupInfo(pName) == nullptr) {
                return 0;
            }
            playingPlayer()->changeAnimationNonStop(pName);
            return 1;
        default:
            return 0;
        }
    }

    const char* apiActorName(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return nullptr;
        }
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
        if (!isFinite3(pAt)) {
            return nullptr;
        }
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
        if (pX != nullptr) {
            *pX = MR::getPlayerStickX();
        }
        if (pY != nullptr) {
            *pY = MR::getPlayerStickY();
        }
    }

    void apiShakeCamera() {
        if (sStageFrames > 0) {
            MR::shakeCameraNormal();
        }
    }

    int32_t apiIsAnimationDone(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager == nullptr || MR::isBckStopped(pLive) ? 1 : 0;
    }

    float apiAnimationFrame(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0.0f;
        }
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager != nullptr ? MR::getBckFrame(pLive) : 0.0f;
    }

    float apiAnimationLength(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0.0f;
        }
        const LiveActor* pLive = asActor(pActor);
        return pLive->mModelManager != nullptr ? MR::getBckFrameMax(pLive) : 0.0f;
    }

    void apiSetAnimationFrame(NebulaActor* pActor, float frame) {
        if (pActor == nullptr || !isFinite(frame)) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        if (pLive->mModelManager != nullptr) {
            MR::setBckFrame(pLive, frame);
        }
    }

    void apiSetAnimationRate(NebulaActor* pActor, float rate) {
        if (pActor == nullptr || !isFinite(rate)) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        if (pLive->mModelManager != nullptr) {
            MR::setBckRate(pLive, rate);
        }
    }

    void apiStopEffect(NebulaActor* pActor, const char* pName) {
        if (pActor == nullptr) {
            return;
        }
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
        if (pActor == nullptr) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        if (pName != nullptr && pLive->mSoundObject != nullptr) {
            MR::stopSound(pLive, pName);
        }
    }

    void apiSetState(NebulaActor* pActor, int32_t state) {
        if (pActor == nullptr) {
            return;
        }
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            pPlugin->mState = state;
            pPlugin->mStateFrames = -1;
        }
    }

    int32_t apiState(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mState : 0;
    }

    int32_t apiStateFrames(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mStateFrames : 0;
    }

    // The game's nerves (Nerve.hpp, NerveRegistry.cpp): a hooked nerve's mNebulaHook is its list's place + 1 here.
    struct NerveHook {
        NebulaNerveCallback mCallback;
        void* mUser;
    };
    struct HookedNerve {
        const Nerve* mNerve;
        List< NerveHook > mHooks;
    };
    List< HookedNerve > sHookedNerves;

    bool runNerveHooks(Spine* pSpine, const Nerve* pNerve, s32 phase) {
        const HookedNerve& rHooked = sHookedNerves.mItems[pNerve->mNebulaHook - 1];
        const nebula::NerveRecord* const pRecord = nebula::nerveRecordOf(pNerve);
        NebulaActor* const pActor = pRecord != nullptr ? asHandle(pRecord->mActorOf(pSpine->mExecutor)) : nullptr;
        const char* const pKey = nebula::nerveKeyOf(pNerve);
        bool runGame = true;
        for (int i = 0; i < rHooked.mHooks.mCount; i++) {
            const NerveHook& rHook = rHooked.mHooks.mItems[i];
            if (rHook.mCallback(pActor, pKey, static_cast< NebulaNervePhase >(phase), pSpine->mStep, rHook.mUser) == 0 &&
                phase == nebula::NervePhase_Frame) {
                runGame = false;
            }
        }
        return runGame;
    }

    Spine* spineOf(NebulaActor* pActor) {
        LiveActor* const pLive = asActor(pActor);
        return pLive != nullptr ? pLive->mSpine : nullptr;
    }

    const char* apiNerve(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return nullptr;
        }
        const Spine* const pSpine = spineOf(pActor);
        return pSpine != nullptr ? nebula::nerveKeyOf(pSpine->getCurrentNerve()) : nullptr;
    }

    int32_t apiNerveFrames(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const Spine* const pSpine = spineOf(pActor);
        return pSpine != nullptr && pSpine->mStep > 0 ? pSpine->mStep : 0;
    }

    // Only to a nerve of the class whose nerve it runs now: that nerve's code calls that class's functions on it.
    int32_t apiSetNerve(NebulaActor* pActor, const char* pName) {
        if (pActor == nullptr || pName == nullptr) {
            return -1;
        }
        Spine* const pSpine = spineOf(pActor);
        const nebula::NerveRecord* const pTo = nebula::findNerve(pName);
        const nebula::NerveRecord* const pNow = pSpine != nullptr ? nebula::nerveRecordOf(pSpine->getCurrentNerve()) : nullptr;
        if (pTo == nullptr || pNow == nullptr || !MR::isEqualString(pTo->mClass, pNow->mClass)) {
            return -1;
        }
        pSpine->setNerve(pTo->mNerve);
        return 0;
    }

    int32_t apiHookNerve(const char* pName, NebulaNerveCallback callback, void* user) {
        if (pName == nullptr) {
            return -1;
        }
        const nebula::NerveRecord* const pRecord = nebula::findNerve(pName);
        if (pRecord == nullptr || callback == nullptr) {
            return -1;
        }
        const Nerve* const pNerve = pRecord->mNerve;
        if (pNerve->mNebulaHook == 0) {
            HookedNerve hooked;
            hooked.mNerve = pNerve;
            sHookedNerves.add(hooked);
            pNerve->mNebulaHook = static_cast< u16 >(sHookedNerves.mCount);
        }
        NerveHook hook = {callback, user};
        sHookedNerves.mItems[pNerve->mNebulaHook - 1].mHooks.add(hook);
        nebula::gNerveHook = runNerveHooks;
        return 0;
    }

    const char* apiNerveAt(int32_t index) {
        return nebula::nerveKeyAt(index);
    }

    void apiGetRotation(NebulaActor* pActor, float* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const Vec3f& rIn = asActor(pActor)->mRotation;
        pOut[0] = rIn.x;
        pOut[1] = rIn.y;
        pOut[2] = rIn.z;
    }

    void apiSetRotation(NebulaActor* pActor, const float* pIn) {
        if (pActor == nullptr || !isFinite3(pIn)) {
            return;
        }
        asActor(pActor)->mRotation.set(pIn[0], pIn[1], pIn[2]);
    }

    void apiGetScale(NebulaActor* pActor, float* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const Vec3f& rIn = asActor(pActor)->mScale;
        pOut[0] = rIn.x;
        pOut[1] = rIn.y;
        pOut[2] = rIn.z;
    }

    void apiSetScale(NebulaActor* pActor, const float* pIn) {
        if (pActor == nullptr || !isFinite3(pIn)) {
            return;
        }
        asActor(pActor)->mScale.set(pIn[0], pIn[1], pIn[2]);
    }

    void apiSetFront(NebulaActor* pActor, const float* pIn) {
        if (pActor == nullptr || !isFinite3(pIn)) {
            return;
        }
        PluginActor* pPlugin = asPluginActor(pActor);
        Vec3f front(pIn[0], pIn[1], pIn[2]);
        if (pPlugin != nullptr && !MR::isNearZero(front)) {
            MR::normalize(&front);
            pPlugin->mFront = front;
            pPlugin->mUseFront = true;
        }
    }

    void apiGetFront(NebulaActor* pActor, float* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        pOut[0] = pPlugin != nullptr ? pPlugin->mFront.x : 0.0f;
        pOut[1] = pPlugin != nullptr ? pPlugin->mFront.y : 0.0f;
        pOut[2] = pPlugin != nullptr ? pPlugin->mFront.z : 1.0f;
    }

    void apiTurnToPlayer(NebulaActor* pActor, float degrees) {
        if (pActor == nullptr || !isFinite(degrees)) {
            return;
        }
        PluginActor* pPlugin = asPluginActor(pActor);
        if (pPlugin != nullptr && playingPlayer() != nullptr) {
            MR::turnDirectionToPlayerDegree(pPlugin, &pPlugin->mFront, degrees);
            pPlugin->mUseFront = true;
        }
    }

    void apiSetTouchable(NebulaActor* pActor, int32_t touchable) {
        if (pActor == nullptr) {
            return;
        }
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            if (touchable != 0) {
                MR::validateHitSensors(pPlugin);
            } else {
                MR::invalidateHitSensors(pPlugin);
            }
        }
    }

    int32_t apiHitPlayer(NebulaActor* pActor, NebulaHit how) {
        if (pActor == nullptr) {
            return 0;
        }
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

    // A column of the object's placement row by its name (Obj_arg0, CommonPath_ID, ShapeModelNo, ...): a number column
    // as a number (its kind's width and sign), a text column as its words.
    int32_t apiPlacementNumber(NebulaActor* pActor, const char* pColumn, double* pValue) {
        if (pActor == nullptr) {
            return 0;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        if (pPlugin == nullptr || pColumn == nullptr || pValue == nullptr) {
            return 0;
        }
        const TableIter row(&pPlugin->mPlacement, pPlugin->mPlacementRow);
        switch (pPlugin->mPlacement.getColumnKind(pColumn)) {
        case TableKind_Int: {
            s32 value = 0;
            if (!row.getValue(pColumn, &value)) {
                return 0;
            }
            *pValue = value;
            return 1;
        }
        case TableKind_Field: {
            u32 value = 0;
            if (!row.getValue(pColumn, &value)) {
                return 0;
            }
            *pValue = value;
            return 1;
        }
        case TableKind_Float: {
            f32 value = 0.0f;
            if (!row.getValue(pColumn, &value)) {
                return 0;
            }
            *pValue = value;
            return 1;
        }
        default:
            return 0;
        }
    }

    const char* apiPlacementText(NebulaActor* pActor, const char* pColumn) {
        if (pActor == nullptr) {
            return nullptr;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        if (pPlugin == nullptr || pColumn == nullptr || pPlugin->mPlacement.getColumnKind(pColumn) != TableKind_Text) {
            return nullptr;
        }
        const char* pText = nullptr;
        return TableIter(&pPlugin->mPlacement, pPlugin->mPlacementRow).getValue(pColumn, &pText) ? pText : nullptr;
    }

    int32_t apiHasSwitch(NebulaActor* pActor, NebulaSwitch which) {
        if (pActor == nullptr) {
            return 0;
        }
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
        if (pActor == nullptr) {
            return 0;
        }
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
        if (pActor == nullptr) {
            return;
        }
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
        if (pActor == nullptr) {
            return 0;
        }
        return MR::isExistRail(asActor(pActor)) ? 1 : 0;
    }

    void apiMoveOnRail(NebulaActor* pActor, float speed) {
        if (pActor == nullptr || !isFinite(speed)) {
            return;
        }
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::moveCoord(pLive, speed);
            MR::moveTransToCurrentRailPos(pLive);
        }
    }

    int32_t apiRailAtEnd(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) && MR::isRailReachedGoal(pLive) ? 1 : 0;
    }

    void apiRailTurnBack(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return;
        }
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::reverseRailDirection(pLive);
        }
    }

    float apiRailCoord(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0.0f;
        }
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) ? MR::getRailCoord(pLive) : 0.0f;
    }

    void apiSetRailCoord(NebulaActor* pActor, float coord) {
        if (pActor == nullptr || !isFinite(coord)) {
            return;
        }
        LiveActor* pLive = asActor(pActor);
        if (MR::isExistRail(pLive)) {
            MR::setRailCoord(pLive, coord);
        }
    }

    float apiRailLength(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0.0f;
        }
        const LiveActor* pLive = asActor(pActor);
        return MR::isExistRail(pLive) ? MR::getRailTotalLength(pLive) : 0.0f;
    }

    int32_t apiRaycast(const float* pFrom, const float* pTo, float* pHit) {
        if (sStageFrames <= 0 || !isFinite3(pFrom) || !isFinite3(pTo)) {
            return 0;
        }
        const Vec3f from(pFrom[0], pFrom[1], pFrom[2]);
        const Vec3f line(pTo[0] - pFrom[0], pTo[1] - pFrom[1], pTo[2] - pFrom[2]);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Triangle triangle;
        if (!MR::getFirstPolyOnLineToMap(&hit, &triangle, from, line)) {
            return 0;
        }
        if (pHit != nullptr) {
            pHit[0] = hit.x;
            pHit[1] = hit.y;
            pHit[2] = hit.z;
        }
        return 1;
    }

    int32_t apiIsOnGround(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const LiveActor* pLive = asActor(pActor);
        if (pLive == playingPlayer()) {
            return MR::isOnGroundPlayer() ? 1 : 0;
        }
        return pLive->mBinder != nullptr && MR::isBindedGround(pLive) ? 1 : 0;
    }

    int32_t apiIsTouchingWall(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0;
        }
        const LiveActor* pLive = asActor(pActor);
        return pLive->mBinder != nullptr && MR::isBindedWall(pLive) ? 1 : 0;
    }

    int32_t apiIsInWater(const float* pAt) {
        if (pAt == nullptr) {
            return 0;
        }
        return sStageFrames > 0 && MR::isInWater(Vec3f(pAt[0], pAt[1], pAt[2])) ? 1 : 0;
    }

    float apiDistanceToPlayer(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return 0.0f;
        }
        return playingPlayer() != nullptr ? MR::calcDistanceToPlayer(asActor(pActor)) : 0.0f;
    }

    void apiGetJointPosition(NebulaActor* pActor, const char* pJoint, float* pOut) {
        if (pActor == nullptr || pOut == nullptr) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        Vec3f at(pLive->mPosition);
        // A name the model lacks is its own position, not the root joint's (MR::copyJointPos's).
        if (pLive->mModelManager != nullptr && pJoint != nullptr &&
            MR::getModel(pLive)->mModelData->findJoint(pJoint) >= 0) {
            MR::copyJointPos(pLive, pJoint, &at);
        }
        pOut[0] = at.x;
        pOut[1] = at.y;
        pOut[2] = at.z;
    }

    void apiPlayerFront(float* pOut) {
        if (pOut == nullptr) {
            return;
        }
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
        if (pActor == nullptr || sStageFrames <= 0) {
            return;
        }
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
        if (pActor == nullptr || !isFinite3(pAt) || sStageFrames <= 0) {
            return;
        }
        const LiveActor* pLive = asActor(pActor);
        const Vec3f at(pAt[0], pAt[1], pAt[2]);
        switch (what) {
        case NEBULA_ITEM_COINS:
            // The holder is made by the first coins declared: a stage with none has no coins to give.
            if (MR::isExistSceneObj(SceneObj_CoinHolder)) {
                MR::appearCoinPop(pLive, at, count);
            }
            break;
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
        if (pActor == nullptr) {
            return 0;
        }
        return pName != nullptr && sStageFrames > 0 && MR::tryStartDemo(asActor(pActor), pName) ? 1 : 0;
    }

    void apiEndCutscene(NebulaActor* pActor, const char* pName) {
        if (pActor == nullptr) {
            return;
        }
        if (pName != nullptr && sStageFrames > 0) {
            MR::endDemo(asActor(pActor), pName);
        }
    }

    // The stage's start too, its opening camera and the player's arrival (the scenario starter): the player has no
    // control until both end, and an information message shown then is lost as the starter hands over.
    int32_t apiIsCutscenePlaying() {
        return sStageFrames > 0 && (MR::isDemoActive() || MR::isStageStateScenarioOpeningCamera() || MR::isExecScenarioStarter() ||
                                    MR::isPowerStarGetDemoActive())
                   ? 1
                   : 0;
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
        if (pGalaxy == nullptr) {
            return;
        }
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

    // "SpinAndStarPointer": the Gateway Luma's grant of the spin and the Star Pointer, which is the story's progress, not
    // a stored flag.
    bool isSpinGrant(const char* pName) {
        return MR::isEqualString(pName, "SpinAndStarPointer");
    }

    int32_t apiIsEventFlagOn(const char* pName) {
        if (pName != nullptr && isSpinGrant(pName)) {
            return GameDataFunction::isPassedStoryEvent("スピン権利") ? 1 : 0;
        }
        return pName != nullptr && GameEventFlagTable::isExist(pName) && GameDataFunction::isOnGameEventFlag(pName) ? 1 : 0;
    }

    void apiSetEventFlagOn(const char* pName) {
        if (pName == nullptr) {
            return;
        }
        if (isSpinGrant(pName)) {
            // The story only moves forward (a later save keeps its progress); the player spins from now, not only from the
            // next stage, which reads the progress as it starts.
            if (!GameDataFunction::isPassedStoryEvent("スピン権利")) {
                GameDataFunction::followStoryEventByName("スピン権利");
            }
            if (playingPlayer() != nullptr) {
                MR::setPlayerSwingPermission(true);
            }
            return;
        }
        if (GameEventFlagTable::isExist(pName)) {
            GameDataFunction::onGameEventFlag(pName);
        }
    }

    int32_t apiHasStar(const char* pGalaxy, int32_t star) {
        if (pGalaxy == nullptr) {
            return 0;
        }
        const char* pName = starGalaxy(pGalaxy);
        return pName != nullptr && star > 0 && MR::isOnGameEventFlagPowerStarSuccess(pName, star) ? 1 : 0;
    }

    // A message id the game or a mod has; else the words themselves (with the game's tags), held here while shown.
    // The message a plugin last showed: a game message's id, or its own words.
    char sMessageId[128];
    char16_t sMessageWords[512];
    bool sMessageIsId = false;
    bool sMessageCentre = false;
    // Asked for while the stage starts (its opening camera, the player's arrival): shown once the player has control,
    // as the starter's hand-over to play would lose it.
    bool sMessagePending = false;

    bool isStageStarting() {
        return MR::isStageStateScenarioOpeningCamera() || MR::isExecScenarioStarter();
    }

    void appearMessage() {
        if (sMessageIsId) {
            if (sMessageCentre) {
                MR::appearInformationMessageCenter(sMessageId, false);
            } else {
                MR::appearInformationMessage(sMessageId, false);
            }
        } else if (sMessageCentre) {
            MR::appearInformationMessageCenter(sMessageWords, false);
        } else {
            MR::appearInformationMessage(sMessageWords, false);
        }
    }

    void apiShowMessage(const char* pText, int32_t centre) {
        if (pText == nullptr || sStageFrames <= 0) {
            return;
        }
        sMessageIsId = std::strlen(pText) < sizeof(sMessageId) && MR::isExistGameMessage(pText);
        if (sMessageIsId) {
            std::memcpy(sMessageId, pText, std::strlen(pText) + 1);
        } else {
            nebula_text::encode(pText, sMessageWords, sizeof(sMessageWords) / sizeof(sMessageWords[0]));
        }
        sMessageCentre = centre != 0;
        sMessagePending = isStageStarting();
        if (!sMessagePending) {
            appearMessage();
        }
    }

    void apiHideMessage() {
        sMessagePending = false;
        if (sStageFrames > 0) {
            MR::disappearInformationMessage();
        }
    }

    // A game sound's number not looked up yet (the sound names are read after plugins start).
    const u32 cSoundNotLooked = 0xFFFFFFFEu;

    struct SoundEffect {
        const char* mName;  // without "@luigi"
        u32 mId;            // its track
        u32 mGameId;        // the game's sound of that name: MR::NameIndex::cNoValue for a new name
        bool mLuigi;        // only while Luigi plays
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
        static const char cLuigi[] = "@luigi";
        const u32 length = static_cast< u32 >(strlen(pName));
        const u32 suffix = sizeof(cLuigi) - 1;
        const bool luigi = length > suffix && MR::isEqualString(pName + length - suffix, cLuigi);
        MR::FormattedName name("%.*s", static_cast< int >(luigi ? length - suffix : length), pName);
        const SoundEffect effect = {copyText(name.c_str()), NebulaMods::trackId(trackName.c_str()), cSoundNotLooked, luigi};
        sSoundEffects.add(effect);
        NebulaMods::sHasSoundEffects = true;
        return 0;
    }

    List< NebulaMods::PlayerAnimation > sPlayerAnimations;
    // The player's tables are built: an animation added now would not be in them.
    bool sPlayerAnimationsTaken = false;

    int32_t apiAddPlayerAnimation(const char* pName, const char* pFile, float rate, int32_t blendFrames) {
        if (sPlayerAnimationsTaken || pName == nullptr || pFile == nullptr || pName[0] == '\0' || pFile[0] == '\0' ||
            !(rate > 0.0f && rate <= 100.0f) || blendFrames < 0 || blendFrames > 600) {
            return -1;
        }
        // Another of the same name gives way: the last added is the one.
        for (int i = 0; i < sPlayerAnimations.mCount; i++) {
            if (MR::isEqualString(sPlayerAnimations.mItems[i].mName, pName)) {
                sPlayerAnimations.mItems[i].mFile = copyText(pFile);
                sPlayerAnimations.mItems[i].mRate = rate;
                sPlayerAnimations.mItems[i].mBlendFrames = static_cast< u32 >(blendFrames);
                return 0;
            }
        }
        const NebulaMods::PlayerAnimation animation = {copyText(pName), copyText(pFile), rate, static_cast< u32 >(blendFrames)};
        sPlayerAnimations.add(animation);
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
        if (pTarget == nullptr) {
            return 0;
        }
        LiveActor* pLive = asActor(pTarget);
        LiveActor* pFrom = pBy != nullptr ? asActor(pBy) : playingPlayer();
        if (pLive == nullptr || pFrom == nullptr || pLive->mSensorKeeper == nullptr || pFrom->mSensorKeeper == nullptr ||
            pLive->mSensorKeeper->mSensorInfosSize <= 0 || pFrom->mSensorKeeper->mSensorInfosSize <= 0) {
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
        if (pActor == nullptr) {
            return nullptr;
        }
        const PluginActor* pPlugin = asPluginActor(pActor);
        return pPlugin != nullptr ? pPlugin->mData : nullptr;
    }

    void apiSetActorData(NebulaActor* pActor, void* pData) {
        if (pActor == nullptr) {
            return;
        }
        if (PluginActor* pPlugin = asPluginActor(pActor)) {
            pPlugin->mData = pData;
        }
    }

    const char* apiActorPlacedName(NebulaActor* pActor) {
        if (pActor == nullptr) {
            return nullptr;
        }
        return asActor(pActor)->mName;
    }

    void apiStopScene(int32_t frames) {
        if (sStageFrames > 0 && frames > 0) {
            MR::stopScene(frames);
        }
    }

    void apiGravityAt(const float* pAt, float* pOut) {
        if (pAt == nullptr || pOut == nullptr) {
            return;
        }
        Vec3f down(0.0f, -1.0f, 0.0f);
        if (sStageFrames > 0) {
            MR::calcGravityVector(MR::getAllLiveActorGroup(), Vec3f(pAt[0], pAt[1], pAt[2]), &down, nullptr, 0);
        }
        pOut[0] = down.x;
        pOut[1] = down.y;
        pOut[2] = down.z;
    }

    int32_t apiIsInArea(const char* pArea, const float* pAt) {
        if (pAt == nullptr) {
            return 0;
        }
        return sStageFrames > 0 && pArea != nullptr && MR::getAreaObj(pArea, Vec3f(pAt[0], pAt[1], pAt[2])) != nullptr ? 1 : 0;
    }

    void apiCameraPosition(float* pOut) {
        if (pOut == nullptr) {
            return;
        }
        const Vec3f at = sStageFrames > 0 ? MR::getCamPos() : Vec3f(0.0f, 0.0f, 0.0f);
        pOut[0] = at.x;
        pOut[1] = at.y;
        pOut[2] = at.z;
    }

    void apiCameraTarget(float* pOut) {
        if (pOut == nullptr) {
            return;
        }
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
        if (pAt == nullptr || pOut == nullptr) {
            return 0;
        }
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
        if (pOut == nullptr) {
            return 0;
        }
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
        if (pGalaxy == nullptr) {
            return;
        }
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
        if (pGalaxy == nullptr) {
            return 0;
        }
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
        if (pLayout == nullptr) {
            return;
        }
        if (show != 0) {
            MR::showLayout(asLayout(pLayout));
        } else {
            MR::hideLayout(asLayout(pLayout));
        }
    }

    void apiLayoutSetText(NebulaLayout* pLayout, const char* pPane, const char* pUtf8) {
        if (pLayout == nullptr) {
            return;
        }
        if (pPane != nullptr && pUtf8 != nullptr) {
            MR::setTextBoxMessageRecursive(asLayout(pLayout), pPane, wordsOf(pUtf8, NebulaArena::findFromRoot(asLayout(pLayout))));
        }
    }

    void apiLayoutSetNumber(NebulaLayout* pLayout, const char* pPane, int32_t number) {
        if (pLayout == nullptr) {
            return;
        }
        if (pPane != nullptr) {
            MR::setTextBoxNumberRecursive(asLayout(pLayout), pPane, number);
        }
    }

    void apiLayoutShowPane(NebulaLayout* pLayout, const char* pPane, int32_t show) {
        if (pLayout == nullptr) {
            return;
        }
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
        if (pLayout == nullptr) {
            return;
        }
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
        if (pLayout == nullptr) {
            return;
        }
        if (pAnimation != nullptr) {
            MR::startAnim(asLayout(pLayout), pAnimation, 0);
        }
    }

    int32_t apiLayoutIsPlaying(NebulaLayout* pLayout) {
        if (pLayout == nullptr) {
            return 0;
        }
        return MR::isAnimStopped(asLayout(pLayout), 0) ? 0 : 1;
    }

    void apiLayoutSetAlpha(NebulaLayout* pLayout, float alpha) {
        if (pLayout == nullptr) {
            return;
        }
        MR::setLayoutAlphaFloat(asLayout(pLayout), alpha);
    }

    // The game's rules as a plugin changes them.
    f32 sGameSpeed = 1.0f;
    f32 sSpeedCarry = 0.0f;
    s32 sStepsAsked = 0;
    bool sSpeedSet = false;
    bool sCameraPosition = false;
    bool sCameraTarget = false;
    f32 sCameraAt[3] = {};
    f32 sCameraLook[3] = {};
    f32 sCameraFov = 0.0f;
    f32 sLastFov = 45.0f;
    TimeLimitLayout* sCountdown = nullptr;
    NebulaMods::TextLine sTextLines[NebulaMods::cTextLines] = {};
    u32 sTextRevision = 0;

    void apiSetGravityScale(float scale) {
        if (isFinite(scale)) {
            NebulaMods::sGravityScale = scale;
        }
    }

    float apiGravityScale() {
        return NebulaMods::sGravityScale;
    }

    int32_t apiTimer(float* pSecondsLeft, float* pSecondsLimit) {
        if (sCountdown == nullptr) {
            return 0;
        }
        const u32 limit = sCountdown->getTimeLimit();
        const u32 time = sCountdown->getTime();
        const f32 step = (1.0f / static_cast< f32 >(NEBULA_STEPS_PER_SECOND));
        if (pSecondsLeft != nullptr) {
            *pSecondsLeft = static_cast< f32 >(limit - time) * step;
        }
        if (pSecondsLimit != nullptr) {
            *pSecondsLimit = static_cast< f32 >(limit) * step;
        }
        return 1;
    }

    int32_t apiSetTimer(float secondsLeft) {
        if (sCountdown == nullptr || !isFinite(secondsLeft)) {
            return -1;
        }
        const u32 limit = sCountdown->getTimeLimit();
        const f32 steps = secondsLeft / (1.0f / static_cast< f32 >(NEBULA_STEPS_PER_SECOND)) + 0.5f;
        const u32 left = steps <= 0.0f ? 0u : steps >= static_cast< f32 >(limit) ? limit : static_cast< u32 >(steps);
        sCountdown->setTime(limit - left);
        return 0;
    }

    void apiPauseTimer(int32_t paused) {
        NebulaMods::sCountdownHeld = paused != 0;
    }

    void apiSetCamera(const float* pPosition, const float* pTarget, float fov) {
        if ((pPosition != nullptr && !isFinite3(pPosition)) || (pTarget != nullptr && !isFinite3(pTarget))) {
            return;
        }
        sCameraPosition = pPosition != nullptr;
        sCameraTarget = pTarget != nullptr;
        for (s32 i = 0; i < 3; i++) {
            sCameraAt[i] = pPosition != nullptr ? pPosition[i] : 0.0f;
            sCameraLook[i] = pTarget != nullptr ? pTarget[i] : 0.0f;
        }
        sCameraFov = fov > 0.0f && fov < 180.0f ? fov : 0.0f;
        NebulaMods::sCameraSet = sCameraPosition || sCameraTarget || sCameraFov > 0.0f;
    }

    void apiSetCameraNear(float distance) {
        NebulaMods::sCameraNear = distance > 0.0f ? (distance < 1.0f ? 1.0f : distance) : 0.0f;
    }

    // The game's camera before a plugin's, recorded each frame it is posed.
    f32 sGameCameraAt[3];
    f32 sGameCameraLook[3];

    int32_t apiGameCamera(float* pPosition, float* pTarget) {
        if (sStageFrames <= 0) {
            return 0;
        }
        for (s32 i = 0; i < 3; i++) {
            if (pPosition != nullptr) {
                pPosition[i] = sGameCameraAt[i];
            }
            if (pTarget != nullptr) {
                pTarget[i] = sGameCameraLook[i];
            }
        }
        return 1;
    }

    NebulaActor* apiTalkingActor() {
        if (sStageFrames <= 0) {
            return nullptr;
        }
        return asHandle(MR::getTalkingActor());
    }

    bool sTalkBoxPlaced = false;
    f32 sTalkBoxX = 0.0f;
    f32 sTalkBoxY = 0.0f;

    void apiPlaceTalkBox(float x, float y, int32_t on) {
        if (on != 0 && (!isFinite(x) || !isFinite(y))) {
            return;
        }
        sTalkBoxPlaced = on != 0;
        sTalkBoxX = x;
        sTalkBoxY = y;
    }

    float apiCameraFov() {
        return sLastFov;
    }

    void apiShowHud(uint32_t parts, int32_t show) {
        parts &= NEBULA_HUD_ALL;
        NebulaMods::sHudHidden = show != 0 ? (NebulaMods::sHudHidden & ~parts) : (NebulaMods::sHudHidden | parts);
    }

    void apiSetGameSpeed(float speed) {
        sGameSpeed = speed >= 1.0f ? 1.0f : speed > 0.0f ? speed : 0.0f;
        sSpeedCarry = 0.0f;
        sSpeedSet = sGameSpeed < 1.0f || sStepsAsked > 0;
    }

    void apiStepGame(int32_t steps) {
        sStepsAsked = steps > 0 ? steps : 0;
        sSpeedSet = sGameSpeed < 1.0f || sStepsAsked > 0;
    }

    int32_t apiDrawText(int32_t line, float x, float y, float size, uint32_t rgba, const char* pUtf8) {
        if (line < 0 || line >= NebulaMods::cTextLines) {
            return -1;
        }
        NebulaMods::TextLine& rLine = sTextLines[line];
        rLine.mShown = pUtf8 != nullptr && pUtf8[0] != 0;
        rLine.mX = x;
        rLine.mY = y;
        rLine.mSize = size > 0.0f ? size : 0.04f;
        rLine.mRgba = rgba;
        // UTF-8 as the game's UTF-16 (wordsOf), cut to the line's room on a whole character.
        s32 out = 0;
        if (rLine.mShown) {
            const size_t length = strlen(pUtf8);
            for (size_t i = 0; i < length;) {
                const u8 lead = static_cast< u8 >(pUtf8[i]);
                const size_t extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
                u32 code = extra == 0 ? lead : extra == 1 ? lead & 0x1F : extra == 2 ? lead & 0x0F : lead & 0x07;
                for (size_t c = 1; c <= extra && i + c < length; c++) {
                    code = (code << 6) | (static_cast< u8 >(pUtf8[i + c]) & 0x3F);
                }
                i += extra + 1;
                const s32 units = code >= 0x10000 ? 2 : 1;
                if (out + units >= NebulaMods::cTextLength) {
                    break;
                }
                if (code >= 0x10000) {
                    code -= 0x10000;
                    rLine.mText[out++] = static_cast< char16_t >(0xD800 | (code >> 10));
                    rLine.mText[out++] = static_cast< char16_t >(0xDC00 | (code & 0x3FF));
                } else {
                    rLine.mText[out++] = static_cast< char16_t >(code);
                }
            }
        }
        rLine.mText[out] = 0;
        sTextRevision++;
        return 0;
    }

    // Version 3: LumaCore (docs/modding/TOOLKIT.md, FIELDS.md, CLASSES.md).
    s32 apiToolkitFind(const char* pName) {
        return pName == nullptr ? -1 : lumacore::findToolkit(pName);
    }

    s32 apiToolkitCall(s32 index, const NebulaValue* pArgs, u32 count, NebulaValue* pOut, u32 outCount) {
        const lumacore::ToolkitRow* row = index < 0 ? nullptr : lumacore::toolkitRow(static_cast< u32 >(index));
        if (row == nullptr || (count != 0 && pArgs == nullptr) || (outCount != 0 && pOut == nullptr)) {
            return -1;
        }
        // The thunk writes its result, then each out-parameter (at most 7: the generator refuses more).
        u32 results = row->result[0] == '\0' || row->result[0] == 'a' ? 0 : 1;  // empty: gives nothing
        for (const char* c = row->params; *c != '\0'; c++) {
            results += (*c >= 'A' && *c <= 'Z') ? 1 : 0;
        }
        NebulaValue out[8];
        for (u32 n = 0; n < 8; n++) {
            out[n] = lumacore::kNone;
        }
        lumacore::takeRefusal();
        row->thunk(pArgs, count, out);
        if (lumacore::takeRefusal()) {
            return -1;
        }
        const u32 written = results < outCount ? results : outCount;
        for (u32 n = 0; n < written && n < 8; n++) {
            pOut[n] = out[n];
        }
        return static_cast< s32 >(written);
    }

    NebulaValue apiActorValue(NebulaActor* pActor) {
        return pActor == nullptr ? lumacore::kNone : lumacore::handle(static_cast< NameObj* >(asActor(pActor)));
    }

    NebulaActor* apiValueActor(const NebulaValue* pValue) {
        if (pValue == nullptr) {
            return nullptr;
        }
        NameObj* const obj = lumacore::object(*pValue, lumacore::kClassId< LiveActor >);
        return obj == nullptr ? nullptr : asHandle(static_cast< LiveActor* >(obj));
    }

    s32 apiFieldFind(const NebulaValue* pObject, const char* pName) {
        if (pObject == nullptr || pName == nullptr) {
            return -1;
        }
        return lumacore::fieldIndex(lumacore::findField(lumacore::classOfValue(*pObject), pName));
    }

    s32 apiFieldGet(const NebulaValue* pObject, s32 field, u32 element, NebulaValue* pOut) {
        const lumacore::FieldRow* row = field < 0 ? nullptr : lumacore::fieldRow(static_cast< u32 >(field));
        if (row == nullptr || pObject == nullptr || pOut == nullptr) {
            return 0;
        }
        return lumacore::readField(*pObject, *row, element, pOut) ? 1 : 0;
    }

    s32 apiFieldSet(const NebulaValue* pObject, s32 field, u32 element, const NebulaValue* pValue) {
        const lumacore::FieldRow* row = field < 0 ? nullptr : lumacore::fieldRow(static_cast< u32 >(field));
        if (row == nullptr || pObject == nullptr || pValue == nullptr) {
            return 0;
        }
        return lumacore::writeField(*pObject, *row, element, *pValue) ? 1 : 0;
    }

    const char* apiClassName(u32 id, u32* pParent) {
        if (id >= lumacore::classCount()) {
            return nullptr;
        }
        const lumacore::ClassRow& row = lumacore::classRow(static_cast< u16 >(id));
        if (pParent != nullptr) {
            *pParent = row.base;
        }
        return row.name;
    }

    s32 apiIsA(const NebulaValue* pObject, const char* pClass) {
        const s32 ancestor = lumacore::findClass(pClass);
        if (pObject == nullptr || ancestor < 0) {
            return 0;
        }
        const u16 id = lumacore::classOfValue(*pObject);
        return id != 0 && lumacore::isA(id, static_cast< u16 >(ancestor)) ? 1 : 0;
    }

    void apiLumaStart(const NebulaModApi* pApi) {
        lumacore::setApi(pApi);
    }

    s32 apiAddModScript(const NebulaPlugin* pMod, const char* pScript) {
        if (pMod == nullptr || pScript == nullptr) {
            return -1;
        }
        char path[256];
        nebula_snprintf(path, sizeof(path), "/LumaScripts/%s/%s.lumascript", pMod->id, pScript);
        char message[256];
        message[0] = '\0';
        const s32 script = lumacore::loadScript(pMod, path, message, sizeof(message));
        if (script < 0) {
            nebula_log("mods: %s: the script %s can't start: %s\n", pMod->id, pScript, message);
            return -1;
        }
        return lumacore::addModScript(script);
    }

    // The registries (LumaRegistry) by kind name.
    s32 registryKindOf(const char* pKind) {
        return pKind != nullptr ? lumacore::registryKind(pKind, static_cast< u32 >(strlen(pKind))) : -1;
    }

    const lumacore::RegistryRow* registryRowAt(const char* pKind, int32_t index) {
        const s32 kind = registryKindOf(pKind);
        if (kind < 0 || index < 0) {
            return nullptr;
        }
        const s32 row = lumacore::registryKindAt(static_cast< u16 >(kind), static_cast< u32 >(index));
        return row >= 0 ? lumacore::registryAt(static_cast< u32 >(row)) : nullptr;
    }

    int32_t apiRegistryCount(const char* pKind) {
        const s32 kind = registryKindOf(pKind);
        return kind < 0 ? 0 : static_cast< int32_t >(lumacore::registryKindCount(static_cast< u16 >(kind)));
    }

    const char* apiRegistryName(const char* pKind, int32_t index) {
        const lumacore::RegistryRow* const pRow = registryRowAt(pKind, index);
        return pRow != nullptr ? pRow->name : nullptr;
    }

    int32_t apiRegistryFind(const char* pKind, const char* pName) {
        const s32 kind = registryKindOf(pKind);
        const s32 row = kind < 0 ? -1 : lumacore::registryFind(static_cast< u16 >(kind), pName);
        if (row < 0) {
            return -1;
        }
        const u32 count = lumacore::registryKindCount(static_cast< u16 >(kind));
        for (u32 i = 0; i < count; i++) {
            if (lumacore::registryKindAt(static_cast< u16 >(kind), i) == row) {
                return static_cast< int32_t >(i);
            }
        }
        return -1;
    }

    int32_t apiRegistryValue(const char* pKind, const char* pName) {
        const s32 kind = registryKindOf(pKind);
        const s32 row = kind < 0 ? -1 : lumacore::registryFind(static_cast< u16 >(kind), pName);
        return row < 0 ? -1 : lumacore::registryAt(static_cast< u32 >(row))->value;
    }

    const char* apiRegistryMod(const char* pKind, int32_t index) {
        const lumacore::RegistryRow* const pRow = registryRowAt(pKind, index);
        return pRow != nullptr && pRow->mod != nullptr ? pRow->mod->id : nullptr;
    }

    s32 apiAddScriptObject(const NebulaPlugin* pMod, const NebulaObjectClass* pShape, const char* pScript) {
        if (pMod == nullptr || pShape == nullptr || pScript == nullptr) {
            return -1;
        }
        char path[256];
        nebula_snprintf(path, sizeof(path), "/LumaScripts/%s/%s.lumascript", pMod->id, pScript);
        char message[256];
        message[0] = '\0';
        const s32 script = lumacore::loadScript(pMod, path, message, sizeof(message));
        if (script < 0) {
            nebula_log("mods: %s: the object %s has no script: %s\n", pMod->id,
                       pShape->name != nullptr ? pShape->name : "?", message);
            return -1;
        }
        sAddingMod = pMod;
        const s32 added = lumacore::addScriptObject(pShape, script);
        sAddingMod = nullptr;
        return added;
    }

    // A mod's script, or a plugin's hooks, on every actor placed by a name (LumaClass).
    s32 apiSetClassScript(const NebulaPlugin* pMod, const char* pName, const char* pScript) {
        if (pMod == nullptr || pName == nullptr || pScript == nullptr) {
            return -1;
        }
        char path[256];
        nebula_snprintf(path, sizeof(path), "/LumaScripts/%s/%s.lumascript", pMod->id, pScript);
        char message[256];
        message[0] = '\0';
        const s32 script = lumacore::loadScript(pMod, path, message, sizeof(message));
        if (script < 0) {
            nebula_log("mods: %s: the class %s has no script: %s\n", pMod->id, pName, message);
            return -1;
        }
        return lumacore::addClassScript(pMod, pName, script);
    }

    s32 apiSetClassHooks(const NebulaPlugin* pMod, const char* pName, const NebulaClassHooks* pHooks) {
        return pMod != nullptr ? lumacore::addClassHooks(pMod, pName, pHooks) : -1;
    }

    // A power-up from a mod's row (luma.json "forms") or a plugin, with its script on the player.
    s32 apiAddForm(const NebulaPlugin* pMod, const NebulaPowerUpClass* pForm, const char* pScript) {
        if (pMod == nullptr || pForm == nullptr) {
            return -1;
        }
        s32 script = -1;
        if (pScript != nullptr && pScript[0] != '\0') {
            char path[256];
            nebula_snprintf(path, sizeof(path), "/LumaScripts/%s/%s.lumascript", pMod->id, pScript);
            char message[256];
            message[0] = '\0';
            script = lumacore::loadScript(pMod, path, message, sizeof(message));
            if (script < 0) {
                nebula_log("mods: %s: the form %s has no script: %s\n", pMod->id, pForm->name != nullptr ? pForm->name : "?", message);
                return -1;
            }
        }
        const NebulaPlugin* const pBefore = sAddingMod;
        sAddingMod = pMod;
        const s32 id = apiAddPowerUp(pForm);
        sAddingMod = pBefore;
        if (id >= 0) {
            sFormScripts.mItems[id - NebulaMods::cFirstPowerUp].mScript = script;
        }
        return id;
    }

    // The fields the host fills are its own: log, option, hook, find_function, store_get, store_set, setting, store_get_text, store_set_text.
    // Set once any mod's rows or code have started.
    bool sAskedForApi = false;

    const NebulaModApi sApi = {
        sizeof(NebulaModApi),
        NEBULA_MOD_API_VERSION,
        0,
        nullptr,
        nullptr,
        apiSubscribe,
        apiRegisterObject,
        nullptr,
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
        apiNerve,
        apiNerveFrames,
        apiSetNerve,
        apiHookNerve,
        apiNerveAt,
        apiGetRotation,
        apiSetRotation,
        apiGetScale,
        apiSetScale,
        apiSetFront,
        apiGetFront,
        apiTurnToPlayer,
        apiSetTouchable,
        apiHitPlayer,
        apiPlacementNumber,
        apiPlacementText,
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
        apiGetTunable,
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
        apiOpenScenarioSelect,
        apiSetStory,
        apiSaveGame,
        apiSetTunable,
        apiTunableAt,
        apiSetGravityScale,
        apiGravityScale,
        apiTimer,
        apiSetTimer,
        apiPauseTimer,
        apiSetCamera,
        apiCameraFov,
        apiShowHud,
        apiSetGameSpeed,
        apiStepGame,
        apiDrawText,
        apiAddTextLike,
        apiAddPlayerAnimation,
        nullptr,  // alloc, release and format: the host's
        nullptr,
        nullptr,
        apiSetCameraNear,
        nullptr,  // unlock_achievement: the host's
        apiGameCamera,
        apiTalkingActor,
        apiPlaceTalkBox,
        apiShowJoint,
        apiJointName,
        apiGetJointMatrix,
        nebula_mod_give_star,
        nebula_mod_give_grand_stars,
        nebula_mod_set_player_character,
        nebula_mod_list_materials,
        nebula_mod_water_point,
        nebula_mod_circle_wipe,
        nebula_mod_grant_spin_and_pointer,
        apiToolkitFind,
        apiToolkitCall,
        apiActorValue,
        apiValueActor,
        apiFieldFind,
        apiFieldGet,
        apiFieldSet,
        apiClassName,
        apiIsA,
        apiLumaStart,
        apiAddScriptObject,
        apiAddModScript,
        apiRegistryCount,
        apiRegistryName,
        apiRegistryFind,
        apiRegistryValue,
        apiRegistryMod,
        apiSetClassScript,
        apiSetClassHooks,
        apiAddForm,
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

    f32 sGravityScale = 1.0f;
    bool sSceneStep = true;
    bool sCameraSet = false;
    f32 sCameraNear = 0.0f;
    u32 sHudHidden = 0;
    bool sCountdownHeld = false;

    u32 cameraPoseSlow(f32* pPosition, f32* pTarget) {
        for (s32 i = 0; i < 3; i++) {
            pPosition[i] = sCameraAt[i];
            pTarget[i] = sCameraLook[i];
        }
        return (sCameraPosition ? 1u : 0u) | (sCameraTarget ? 2u : 0u);
    }

    f32 cameraFov(f32 fovy) {
        sLastFov = sCameraSet && sCameraFov > 0.0f ? sCameraFov : fovy;
        return sLastFov;
    }

    void setCountdown(TimeLimitLayout* pLayout, bool shown) {
        if (shown) {
            sCountdown = pLayout;
        } else if (sCountdown == pLayout) {
            sCountdown = nullptr;
        }
    }

    void sceneEnded() {
        // Scripts on the scene's objects end first, then every handle made in it reads as none.
        lumacore::scriptsSceneEnded();
        lumacore::nextGeneration();
        sCountdown = nullptr;
        sMessagePending = false;
        sTalkBoxPlaced = false;
    }

    void recordGameCamera(f32 x, f32 y, f32 z, f32 targetX, f32 targetY, f32 targetZ) {
        sGameCameraAt[0] = x;
        sGameCameraAt[1] = y;
        sGameCameraAt[2] = z;
        sGameCameraLook[0] = targetX;
        sGameCameraLook[1] = targetY;
        sGameCameraLook[2] = targetZ;
    }

    bool talkBoxPlace(f32* pX, f32* pY) {
        *pX = sTalkBoxX;
        *pY = sTalkBoxY;
        return sTalkBoxPlaced;
    }

    const TextLine& textLine(s32 line) {
        return sTextLines[line];
    }

    u32 textRevision() {
        return sTextRevision;
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
        if (sMessagePending && !isStageStarting()) {
            sMessagePending = false;
            appearMessage();
        }
        fire(NEBULA_MOD_EVENT_STAGE_FRAME);
        // This frame's step, after the plugins have had their say: every frame at the game's speed; a share of them
        // slower; while held, the steps asked for.
        if (sSpeedSet) {
            if (sStepsAsked > 0) {
                sStepsAsked--;
                sSceneStep = true;
            } else {
                sSpeedCarry += sGameSpeed;
                sSceneStep = sSpeedCarry >= 1.0f;
                if (sSceneStep) {
                    sSpeedCarry -= 1.0f;
                }
            }
            sSpeedSet = sGameSpeed < 1.0f || sStepsAsked > 0;
        } else {
            sSceneStep = true;
        }
    }

    StageFrame::~StageFrame() {
        sStageFrames--;
    }

    InStage::InStage(bool on) : mOn(on) {
        if (mOn) {
            sStageFrames++;
        }
    }

    InStage::~InStage() {
        if (mOn) {
            sStageFrames--;
        }
    }

    // A form's script instance, while the scene it started in lasts.
    FormScript* formScript(u16 mode) {
        const int index = static_cast< int >(mode) - NebulaMods::cFirstPowerUp;
        return index >= 0 && index < sFormScripts.mCount ? &sFormScripts.mItems[index] : nullptr;
    }

    lumascript::Instance* formInstance(u16 mode) {
        FormScript* const pForm = formScript(mode);
        return pForm != nullptr && pForm->mInstance != nullptr && pForm->mGeneration == lumacore::scriptsSceneGeneration() ? pForm->mInstance : nullptr;
    }

    s32 powerUpStart(u16 mode, LiveActor* pPlayer) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower == nullptr) {
            return 0;
        }
        if (FormScript* const pForm = formScript(mode); pForm != nullptr && pForm->mScript >= 0) {
            // Got again: a new instance, its vars from their start.
            if (lumascript::Instance* const pOld = formInstance(mode)) {
                lumacore::endInstance(pOld);
            }
            pForm->mInstance = lumacore::startInstance(pForm->mScript, lumacore::handle(pPlayer));
            pForm->mGeneration = lumacore::scriptsSceneGeneration();
            if (pForm->mInstance != nullptr) {
                (void)lumascript::dispatch(pForm->mInstance, lumascript::Event_Start, -1, nullptr, 0, nullptr);
            }
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
        if (lumascript::Instance* const pInstance = formInstance(mode)) {
            lumascript::frame(pInstance);
        }
    }

    void powerUpEnd(u16 mode, LiveActor* pPlayer) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower == nullptr) {
            return;
        }
        if (lumascript::Instance* const pInstance = formInstance(mode)) {
            (void)lumascript::dispatch(pInstance, lumascript::Event_End, -1, nullptr, 0, nullptr);
            lumacore::endInstance(pInstance);
            formScript(mode)->mInstance = nullptr;
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

    bool isPowerUp(u16 mode) {
        return powerUpOf(mode) != nullptr;
    }

    u16 powerUpActsLike(u16 mode) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        return pPower != nullptr ? static_cast< u16 >(pPower->acts_like) : mode;
    }

    u16 powerUpLook(u16 mode) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        return pPower != nullptr ? static_cast< u16 >(pPower->looks_like) : mode;
    }

    // The game's: Flying wears frame 1, Fire frame 2, every other his own (0). A mod's power-up: its colour frame, else its
    // look's.
    u32 powerUpColorFrame(u16 mode) {
        const NebulaPowerUpClass* pPower = powerUpOf(mode);
        if (pPower != nullptr && pPower->color_frame > 0) {
            return static_cast< u32 >(pPower->color_frame);
        }
        switch (powerUpLook(mode)) {
        case NEBULA_POWER_UP_FLYING: return 1;
        case NEBULA_POWER_UP_FIRE: return 2;
        default: return 0;
        }
    }

    const char16_t* text(const char* pId, const char** ppLike) {
        for (int i = sTexts.mCount - 1; i >= 0; i--) {
            if (MR::isEqualString(sTexts.mItems[i].mId, pId)) {
                if (ppLike != nullptr) {
                    *ppLike = sTexts.mItems[i].mLike;
                }
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

    s32 takePlayerAnimations(const PlayerAnimation** ppAnimations) {
        sPlayerAnimationsTaken = true;
        *ppAnimations = sPlayerAnimations.mItems;
        return sPlayerAnimations.mCount;
    }

    // By name: the last mod's for all players (Luigi's own goes in by soundFor).
    u32 soundEffectSlow(const char* pName) {
        for (int i = sSoundEffects.mCount - 1; i >= 0; i--) {
            if (!sSoundEffects.mItems[i].mLuigi && MR::isEqualString(sSoundEffects.mItems[i].mName, pName)) {
                return sSoundEffects.mItems[i].mId;
            }
        }
        return static_cast< u32 >(-1);
    }

    u32 soundFor(u32 id, bool luigi) {
        const AudSoundNameConverter* pNames = AudSingletonHolder< AudSoundNameConverter >::get();
        for (int i = 0; i < sSoundEffects.mCount; i++) {
            SoundEffect& rEffect = sSoundEffects.mItems[i];
            if (rEffect.mGameId == cSoundNotLooked && pNames != nullptr) {
                rEffect.mGameId = pNames->mIndex.find(rEffect.mName);
            }
        }
        // Luigi's own first while he plays: in place of the game's sound, or of the mod's for every player.
        for (int pass = luigi ? 0 : 1; pass < 2; pass++) {
            const bool luigiPass = pass == 0;
            for (int i = sSoundEffects.mCount - 1; i >= 0; i--) {
                const SoundEffect& rEffect = sSoundEffects.mItems[i];
                if (rEffect.mLuigi != luigiPass) {
                    continue;
                }
                if (rEffect.mGameId == id || (luigiPass && soundEffectSlow(rEffect.mName) == id)) {
                    return rEffect.mId;
                }
            }
        }
        return id;
    }

    const char* soundName(u32 id) {
        const AudSoundNameConverter* pNames = AudSingletonHolder< AudSoundNameConverter >::get();
        return pNames != nullptr ? pNames->mIndex.nameOf(id) : nullptr;
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
        if (pEntry == nullptr && sObjects.mCount != 0) {
            // What a change turns it into: that object's files.
            const char* const pBecomes = objectBecomes(NEBULA_OBJECTS_PLACED, objectLike(pName));
            pEntry = pBecomes != nullptr ? findObject(pBecomes) : nullptr;
        }
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

    const char* modObjectName(const char* pPlacedName) {
        if (sObjects.mCount == 0) {
            return nullptr;
        }
        const ObjectEntry* pEntry = findObject(pPlacedName);
        if (pEntry == nullptr) {
            const char* const pBecomes = objectBecomes(NEBULA_OBJECTS_PLACED, objectLike(pPlacedName));
            pEntry = pBecomes != nullptr ? findObject(pBecomes) : nullptr;
        }
        return pEntry != nullptr ? pEntry->mName : nullptr;
    }

}  // namespace NebulaMods
