#include "Game/System/NebulaMods.hpp"
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

    List< Subscriber > sSubscribers[NEBULA_MOD_EVENT_COUNT];
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

    // A mod's text kept: for the run (the root arena: tables, and names the next scene reads), or with pArena.
    const char* copyText(const char* pText, NebulaArena* pArena = NebulaArena::root()) {
        if (pText == nullptr) {
            return nullptr;
        }
        const size_t length = strlen(pText) + 1;
        char* pCopy = new (pArena, 0) char[length];
        memcpy(pCopy, pText, length);
        return pCopy;
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

    int32_t apiAddPowerUp(const NebulaPowerUpClass* pClass) {
        if (pClass == nullptr || pClass->size < sizeof(NebulaPowerUpClass)) {
            return -1;
        }
        NebulaPowerUpClass power = *pClass;
        power.name = copyText(pClass->name);
        power.music = copyText(pClass->music);
        power.model = copyText(power.model);
        // Under it, one of the game's forms (another number: the normal player).
        const s32 acts = power.acts_like;
        if (!((acts >= 0 && acts <= 7) || acts == 9)) {
            power.acts_like = 0;
        }
        sPowerUps.add(power);
        return NebulaMods::cFirstPowerUp + sPowerUps.mCount - 1;
    }

    const NebulaPowerUpClass* powerUpOf(u16 mode) {
        const int index = static_cast< int >(mode) - NebulaMods::cFirstPowerUp;
        return index >= 0 && index < sPowerUps.mCount ? &sPowerUps.mItems[index] : nullptr;
    }

    // A game sound's number not looked up yet (the sound names are read after the mods' rows).
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
        if (pName == nullptr || pName[0] == '\0' || pLike == nullptr || pLike[0] == '\0') {
            return -1;
        }
        const ObjectClass row = {copyText(pName), copyText(pLike)};
        sObjectClasses.add(row);
        return 0;
    }

    // Set once the host asked for the table: some mod has rows.
    bool sAskedForApi = false;

    // The player's state at the last stage frame, for the edges stageFrame fires on.
    bool sSwingBefore = false;
    bool sGroundBefore = true;
    bool sRisingBefore = false;

    const NebulaModApi sApi = {
        sizeof(NebulaModApi),
        NEBULA_MOD_API_VERSION,
        apiSubscribe,
        apiAddGalaxy,
        apiAddHungryLuma,
        apiAddText,
        apiAddTextLike,
        apiAddPowerUp,
        apiAddMusic,
        apiSetStageMusic,
        apiSetAreaMusic,
        apiSetEventFlag,
        apiSetCometSchedule,
        apiSetObjectChange,
        apiSetCometTimeLimit,
        apiSetStageRules,
        apiSetPlayerConstant,
        apiSetObjectClass,
        apiAddSoundEffect,
        apiAddPlayerAnimation,
        apiSetStory,
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

    void stageFrame() {
        if (sWanted[NEBULA_MOD_EVENT_PLAYER_SPIN] || sWanted[NEBULA_MOD_EVENT_PLAYER_JUMP] ||
            sWanted[NEBULA_MOD_EVENT_PLAYER_LAND]) {
            // The player in a stage being played: none on the file select screen.
            const bool player = !MR::isStageFileSelect() && MR::isExistSceneObj(SceneObj_MarioHolder);
            const bool swing = player && MR::isPlayerSwingAction();
            const bool ground = player && MR::isOnGroundPlayer();
            const bool rising = player && MR::isPlayerJumpRising();
            if (swing && !sSwingBefore) {
                fire(NEBULA_MOD_EVENT_PLAYER_SPIN);
            }
            if (rising && !sRisingBefore) {
                fire(NEBULA_MOD_EVENT_PLAYER_JUMP);
            }
            if (ground && !sGroundBefore) {
                fire(NEBULA_MOD_EVENT_PLAYER_LAND);
            }
            sSwingBefore = swing;
            sGroundBefore = ground;
            sRisingBefore = rising;
        }
        fire(NEBULA_MOD_EVENT_STAGE_FRAME);
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
        return pPower->frames;
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
        if (pClass != nullptr && !MR::isExistObjectOrLayoutArchive(pClass->mName)) {
            nebula_log("mods: left out: %s: it has no archive %s\n", pName, pClass->mName);
            return true;
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
}  // namespace NebulaMods
