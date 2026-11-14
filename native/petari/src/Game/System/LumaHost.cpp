// The mods' scripts in the game: programs by mod and path, an instance per object
// that runs one, events between scripts, saved values through the mod's store.
#include "Game/System/LumaHost.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaScript.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <cstring>
#include <new>

namespace {
    // How deep events may start events (an event's handler emitting one) before the deeper ones are dropped.
    NEBULA_TUNABLE(u32, cEmitDepth, 8);

    struct ScriptFile {
        const NebulaPlugin* mMod;
        const char* mPath;
        lumascript::Program* mProgram;
        const u8* mBytes;
    };

    // Grows in the root arena: scripts live for the run.
    template < typename T >
    struct Rows {
        T* mItems = nullptr;
        u32 mCount = 0;
        u32 mRoom = 0;

        void add(const T& rItem) {
            if (mCount == mRoom) {
                const u32 room = mRoom == 0 ? 8 : mRoom * 2;
                T* items = new (NebulaArena::root(), 0) T[room];
                for (u32 i = 0; i < mCount; i++) {
                    items[i] = mItems[i];
                }
                delete[] mItems;
                mItems = items;
                mRoom = room;
            }
            mItems[mCount++] = rItem;
        }
    };

    Rows< ScriptFile > sScripts;
    const NebulaModApi* sApi = nullptr;
    u32 sEmitDepth = 0;
    bool sEmitLogged = false;
    // Saved text read back: lasts until the next read (the evaluator keeps its own copy).
    char sSavedText[256];

    // Every living instance, for events (an intrusive list: no lookups per event).
    struct Living {
        lumascript::Instance* mInstance;
        lumascript::Program* mProgram;
        Living* mNext;
        Living* mPrev;
        bool mGlobal;  // a mod-wide script's: lives for the run
    };
    Living* sLiving = nullptr;

    // Script events by name, one id each for every script (resolved as scripts load).
    Rows< const char* > sEventNames;

    // The game's events by their script names, in NebulaModEvent order ("frame" is the scripts' own: no name here).
    const char* const cGameEvents[] = {
        "",                  "stage_frame",    "stage_start",     "player_damage", "player_miss",       "power_up",
        "coin",              "star_bit",       "star_get",        "cutscene_start", "cutscene_end",     "actor_appear",
        "actor_kill",        "player_spin",    "player_jump",     "player_land",   "pause",             "stage_end",
        "game_save",         "power_star_appear", "talk_line",    "talk_choice",   "cutscene_part",     "camera_change",
        "sound_start",       "effect_start",   "screen",          "actor_message", "player_action",
    };
    static_assert(sizeof(cGameEvents) / sizeof(cGameEvents[0]) == NEBULA_MOD_EVENT_COUNT,
                  "a script name for every game event");
    bool sGameSubscribed[NEBULA_MOD_EVENT_COUNT] = {};
    bool sFrameSubscribed = false;

    NebulaValue noneValue() {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        return v;
    }

    // A game event for every script that answers it: (value, name, actor). What a handler returns (a number or a
    // bool) becomes the event's value, as a plugin's callback's does.
    int32_t onGameEvent(const NebulaModEventData* pData, void* pUser) {
        const s32 event = static_cast< s32 >(reinterpret_cast< uintptr_t >(pUser));
        int32_t value = pData->value;
        NebulaValue args[3];
        args[1] = noneValue();
        if (pData->name != nullptr) {
            args[1].kind = NEBULA_KIND_TEXT;
            args[1].u.s = pData->name;
        }
        args[2] = pData->actor != nullptr && sApi != nullptr ? sApi->actor_value(pData->actor) : noneValue();
        for (Living* l = sLiving; l != nullptr;) {
            Living* const next = l->mNext;
            args[0] = noneValue();
            args[0].kind = NEBULA_KIND_INT;
            args[0].u.i = value;
            NebulaValue result = noneValue();
            if (lumascript::dispatch(l->mInstance, lumascript::Event_Game, event, args, 3, &result)) {
                if (result.kind == NEBULA_KIND_INT || result.kind == NEBULA_KIND_BOOL) {
                    value = static_cast< int32_t >(result.u.i);
                } else if (result.kind == NEBULA_KIND_FLOAT) {
                    value = static_cast< int32_t >(result.u.f);
                }
            }
            l = next;
        }
        return value;
    }

    // Every frame (menus too): the mod-wide scripts' waits, states and "frame".
    int32_t onFrame(const NebulaModEventData* pData, void*) {
        for (Living* l = sLiving; l != nullptr;) {
            Living* const next = l->mNext;
            if (l->mGlobal) {
                lumascript::frame(l->mInstance);
            }
            l = next;
        }
        return pData->value;
    }

    // The game's events a program answers: subscribed once each, for every script.
    void subscribeGameEvents(const lumascript::Program* pProgram) {
        if (sApi == nullptr) {
            return;
        }
        for (s32 e = 1; e < NEBULA_MOD_EVENT_COUNT; e++) {
            if (!sGameSubscribed[e] && lumascript::hasEvent(pProgram, lumascript::Event_Game, e)) {
                sGameSubscribed[e] = true;
                sApi->subscribe(static_cast< NebulaModEvent >(e), onGameEvent, reinterpret_cast< void* >(static_cast< uintptr_t >(e)));
            }
        }
    }

    char* copyText(const char* pText) {
        const u32 length = static_cast< u32 >(std::strlen(pText));
        char* copy = new (NebulaArena::root(), 0) char[length + 1];
        std::memcpy(copy, pText, length + 1);
        return copy;
    }

    // Saved values are kept as text in the mod's store under "script:<name>", so every kind survives.
    void saveKey(char* pKey, u32 size, const char* pName) {
        nebula_snprintf(pKey, size, "script:%s", pName);
    }
}  // namespace

namespace lumacore {
    void setApi(const NebulaModApi* pApi) {
        sApi = pApi;
    }

    const NebulaModApi* api() {
        return sApi;
    }

    const NebulaPlugin* runningMod() {
        return static_cast< const NebulaPlugin* >(lumascript::owner(lumascript::running()));
    }

    u32 eventId(const char* pName) {
        for (u32 i = 0; i < sEventNames.mCount; i++) {
            if (MR::isEqualString(sEventNames.mItems[i], pName)) {
                return i;
            }
        }
        sEventNames.add(copyText(pName));
        return sEventNames.mCount - 1;
    }

    s32 gameEvent(const char* pName) {
        for (s32 e = 1; e < NEBULA_MOD_EVENT_COUNT; e++) {
            if (MR::isEqualString(cGameEvents[e], pName)) {
                return e;
            }
        }
        return -1;
    }

    s32 loadScript(const NebulaPlugin* pMod, const char* pPath, char* pMessage, u32 messageSize) {
        for (u32 i = 0; i < sScripts.mCount; i++) {
            if (sScripts.mItems[i].mMod == pMod && MR::isEqualString(sScripts.mItems[i].mPath, pPath)) {
                return static_cast< s32 >(i);
            }
        }
        const NebulaHostServices* const host = nebula_host();
        NebulaAssetEntry entry;
        if (host->asset_find(host->user, pPath, &entry) == 0 || entry.folder != 0) {
            nebula_snprintf(pMessage, messageSize, "no file %s", pPath);
            return -1;
        }
        const u8* const bytes = static_cast< const u8* >(host->asset_map(host->user, entry.index));
        if (bytes == nullptr) {
            nebula_snprintf(pMessage, messageSize, "%s can't be read", pPath);
            return -1;
        }
        lumascript::Program* const program =
            lumascript::load(bytes, entry.length, pMod->id, pMessage, messageSize);
        if (program == nullptr) {
            return -1;
        }
        lumascript::setOwner(program, pMod);
        subscribeGameEvents(program);
        ScriptFile file;
        file.mMod = pMod;
        file.mPath = copyText(pPath);
        file.mProgram = program;
        file.mBytes = bytes;
        sScripts.add(file);
        return static_cast< s32 >(sScripts.mCount - 1);
    }

    lumascript::Program* script(s32 index) {
        return index >= 0 && static_cast< u32 >(index) < sScripts.mCount ? sScripts.mItems[index].mProgram : nullptr;
    }

    void forEachScriptProgram(void (*pDo)(lumascript::Program*)) {
        for (u32 i = 0; i < sScripts.mCount; i++) {
            pDo(sScripts.mItems[i].mProgram);
        }
    }

    const NebulaPlugin* scriptMod(const lumascript::Program* pProgram) {
        for (u32 i = 0; i < sScripts.mCount; i++) {
            if (sScripts.mItems[i].mProgram == pProgram) {
                return sScripts.mItems[i].mMod;
            }
        }
        return nullptr;
    }

    lumascript::Instance* startInstance(s32 script, const NebulaValue& rSelf) {
        lumascript::Program* const program = lumacore::script(script);
        if (program == nullptr) {
            return nullptr;
        }
        lumascript::Instance* const instance = lumascript::createInstance(program, rSelf);
        if (instance == nullptr) {
            return nullptr;
        }
        Living* const living = new (NebulaArena::root(), 0) Living();
        living->mInstance = instance;
        living->mProgram = program;
        living->mGlobal = false;
        living->mPrev = nullptr;
        living->mNext = sLiving;
        if (sLiving != nullptr) {
            sLiving->mPrev = living;
        }
        sLiving = living;
        return instance;
    }

    void endInstance(lumascript::Instance* pInstance) {
        for (Living* l = sLiving; l != nullptr; l = l->mNext) {
            if (l->mInstance == pInstance) {
                if (l->mPrev != nullptr) {
                    l->mPrev->mNext = l->mNext;
                } else {
                    sLiving = l->mNext;
                }
                if (l->mNext != nullptr) {
                    l->mNext->mPrev = l->mPrev;
                }
                delete l;
                break;
            }
        }
        lumascript::destroyInstance(pInstance);
    }

    void scriptEmit(u32 event, const NebulaValue& rValue) {
        if (sEmitDepth >= ::cEmitDepth) {
            if (!sEmitLogged) {
                sEmitLogged = true;
                nebula_log("mods: events started events %u deep (\"%s\"); the deeper ones are dropped\n", ::cEmitDepth,
                           event < sEventNames.mCount ? sEventNames.mItems[event] : "?");
            }
            return;
        }
        sEmitDepth++;
        for (Living* l = sLiving; l != nullptr;) {
            Living* const next = l->mNext;  // a handler may end its own instance (never another's mid-walk)
            (void)lumascript::dispatch(l->mInstance, lumascript::Event_Script, static_cast< s32 >(event), &rValue, 1, nullptr);
            l = next;
        }
        sEmitDepth--;
    }

    NebulaValue scriptSaved(const char* pMod, const char* pName, u8 kind) {
        // Never saved yet: the kind's zero, as a variable starts.
        NebulaValue v = lumascript::zeroOf(kind);
        const NebulaPlugin* mod = nullptr;
        for (u32 i = 0; i < sScripts.mCount; i++) {
            if (MR::isEqualString(sScripts.mItems[i].mMod->id, pMod)) {
                mod = sScripts.mItems[i].mMod;
                break;
            }
        }
        if (mod == nullptr || sApi == nullptr) {
            return v;
        }
        char key[128];
        saveKey(key, sizeof(key), pName);
        char* const text = sSavedText;
        if (sApi->store_get_text(mod, key, text, sizeof(sSavedText)) <= 0) {
            return v;
        }
        switch (kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT: {
            s64 n = 0;
            bool negative = text[0] == '-';
            for (const char* c = text + (negative ? 1 : 0); *c >= '0' && *c <= '9'; c++) {
                n = static_cast< s64 >(static_cast< u64 >(n) * 10u + static_cast< u64 >(*c - '0'));
            }
            v.kind = kind;
            v.u.i = negative ? -n : n;
            break;
        }
        case NEBULA_KIND_FLOAT: {
            // Kept as the number's bits ("f" and 16 hex digits): read back exactly.
            u64 bits = 0;
            for (const char* c = text + (text[0] == 'f' ? 1 : 0); *c != '\0'; c++) {
                const char d = *c;
                const u32 digit = d >= '0' && d <= '9' ? static_cast< u32 >(d - '0')
                                  : d >= 'a' && d <= 'f' ? static_cast< u32 >(d - 'a' + 10)
                                                         : 16u;
                if (digit == 16u) {
                    break;
                }
                bits = (bits << 4) | digit;
            }
            v.kind = NEBULA_KIND_FLOAT;
            std::memcpy(&v.u.f, &bits, sizeof(bits));
            break;
        }
        case NEBULA_KIND_TEXT:
            v.kind = NEBULA_KIND_TEXT;
            v.u.s = text;
            break;
        default:
            break;
        }
        return v;
    }

    void setScriptSaved(const char* pMod, const char* pName, const NebulaValue& rValue) {
        const NebulaPlugin* mod = nullptr;
        for (u32 i = 0; i < sScripts.mCount; i++) {
            if (MR::isEqualString(sScripts.mItems[i].mMod->id, pMod)) {
                mod = sScripts.mItems[i].mMod;
                break;
            }
        }
        if (mod == nullptr || sApi == nullptr) {
            return;
        }
        char key[128];
        saveKey(key, sizeof(key), pName);
        char text[256];
        switch (rValue.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            nebula_snprintf(text, sizeof(text), "%lld", static_cast< long long >(rValue.u.i));
            break;
        case NEBULA_KIND_FLOAT: {
            u64 bits;
            std::memcpy(&bits, &rValue.u.f, sizeof(bits));
            nebula_snprintf(text, sizeof(text), "f%016llx", static_cast< unsigned long long >(bits));
            break;
        }
        case NEBULA_KIND_TEXT:
            nebula_snprintf(text, sizeof(text), "%s", rValue.u.s != nullptr ? rValue.u.s : "");
            break;
        default:
            text[0] = '\0';
            break;
        }
        sApi->store_set_text(mod, key, text);
    }
}  // namespace lumacore

namespace {
    // An object a script runs: luma.json "objects" with "script", made as plugin objects are (NebulaObjectClass), its
    // callbacks running the script's handlers.
    struct ScriptObject {
        NebulaObjectClass mClass;
        s32 mScript;
    };

    lumascript::Instance* instanceOf(NebulaActor* pActor) {
        return sApi != nullptr ? static_cast< lumascript::Instance* >(sApi->actor_data(pActor)) : nullptr;
    }

    NebulaValue actorValue(NebulaActor* pActor) {
        return lumacore::handle(reinterpret_cast< NameObj* >(pActor));
    }

    bool runHandler(NebulaActor* pActor, lumascript::Event event, s32 number, const NebulaValue* pArgs, u32 count,
                    NebulaValue* pResult) {
        lumascript::Instance* const instance = instanceOf(pActor);
        return instance != nullptr && lumascript::dispatch(instance, event, number, pArgs, count, pResult);
    }

    void scriptStart(NebulaActor* pActor, void* pUser) {
        const ScriptObject* const object = static_cast< const ScriptObject* >(pUser);
        lumascript::Instance* const instance = lumacore::startInstance(object->mScript, actorValue(pActor));
        sApi->set_actor_data(pActor, instance);
        runHandler(pActor, lumascript::Event_Start, -1, nullptr, 0, nullptr);
    }

    void scriptUpdate(NebulaActor* pActor, void*) {
        lumascript::Instance* const instance = instanceOf(pActor);
        if (instance != nullptr) {
            lumascript::frame(instance);
        }
    }

    void scriptTouch(NebulaActor* pActor, void*) {
        runHandler(pActor, lumascript::Event_Touch, -1, nullptr, 0, nullptr);
    }

    bool truthyResult(const NebulaValue& rResult) {
        return (rResult.kind == NEBULA_KIND_BOOL || rResult.kind == NEBULA_KIND_INT) && rResult.u.i != 0;
    }

    int32_t scriptAttacked(NebulaActor* pActor, int32_t how, void*) {
        NebulaValue arg = noneValue();
        arg.kind = NEBULA_KIND_INT;
        arg.u.i = how;
        NebulaValue result = noneValue();
        runHandler(pActor, lumascript::Event_Attacked, -1, &arg, 1, &result);
        return truthyResult(result) ? 1 : 0;
    }

    void scriptAppear(NebulaActor* pActor, void*) {
        runHandler(pActor, lumascript::Event_Appear, -1, nullptr, 0, nullptr);
    }

    void scriptKill(NebulaActor* pActor, void*) {
        runHandler(pActor, lumascript::Event_Kill, -1, nullptr, 0, nullptr);
    }

    // A message to the object: "message:<n>" (from) for that message, else "message" (kind, from). True takes it.
    int32_t scriptMessage(NebulaActor* pActor, int32_t message, NebulaActor* pFrom, void*) {
        NebulaValue args[2];
        args[0] = noneValue();
        args[0].kind = NEBULA_KIND_INT;
        args[0].u.i = message;
        args[1] = pFrom != nullptr ? actorValue(pFrom) : noneValue();
        NebulaValue result = noneValue();
        if (runHandler(pActor, lumascript::Event_Message, message, &args[1], 1, &result)) {
            return truthyResult(result) ? 1 : 0;
        }
        result = noneValue();
        runHandler(pActor, lumascript::Event_Message, -1, args, 2, &result);
        return truthyResult(result) ? 1 : 0;
    }
}  // namespace

namespace lumacore {
    s32 addScriptObject(const NebulaObjectClass* pShape, s32 script) {
        lumascript::Program* const program = lumacore::script(script);
        if (program == nullptr || sApi == nullptr) {
            return -1;
        }
        ScriptObject* const object = new (NebulaArena::root(), 0) ScriptObject();
        object->mClass = *pShape;
        object->mClass.size = sizeof(NebulaObjectClass);
        object->mClass.name = copyText(pShape->name);
        object->mClass.archive = pShape->archive != nullptr ? copyText(pShape->archive) : nullptr;
        object->mClass.user = object;
        object->mClass.start = scriptStart;
        object->mClass.update = scriptUpdate;
        object->mClass.attacked = nullptr;
        object->mScript = script;
        // Only what the script answers: an object that ignores touches stays as plain as the game's.
        object->mClass.touch_player = lumascript::hasEvent(program, lumascript::Event_Touch, -1) ? scriptTouch : nullptr;
        object->mClass.attacked_by = lumascript::hasEvent(program, lumascript::Event_Attacked, -1) ? scriptAttacked : nullptr;
        object->mClass.appeared = lumascript::hasEvent(program, lumascript::Event_Appear, -1) ? scriptAppear : nullptr;
        object->mClass.killed = lumascript::hasEvent(program, lumascript::Event_Kill, -1) ? scriptKill : nullptr;
        object->mClass.message = lumascript::hasEvent(program, lumascript::Event_Message, -1) ? scriptMessage : nullptr;
        return sApi->register_object(&object->mClass);
    }

    s32 addModScript(s32 script) {
        lumascript::Program* const program = lumacore::script(script);
        if (program == nullptr || sApi == nullptr) {
            return -1;
        }
        lumascript::Instance* const instance = startInstance(script, noneValue());
        if (instance == nullptr) {
            return -1;
        }
        sLiving->mGlobal = true;  // startInstance put it first
        if (!sFrameSubscribed) {
            sFrameSubscribed = true;
            sApi->subscribe(NEBULA_MOD_EVENT_FRAME, onFrame, nullptr);
        }
        (void)lumascript::dispatch(instance, lumascript::Event_Start, -1, nullptr, 0, nullptr);
        return 0;
    }

    u32 sSceneGeneration = 0;

    u32 scriptsSceneGeneration() {
        return sSceneGeneration;
    }

    void scriptsSceneEnded() {
        sSceneGeneration++;
        for (Living* l = sLiving; l != nullptr;) {
            Living* const next = l->mNext;
            if (!l->mGlobal) {
                endInstance(l->mInstance);
            }
            l = next;
        }
    }
}  // namespace lumacore
