// The mods' scripts in the game (agents/LUMACORE-PLAN.md 8.5, 8.6): programs by mod and path, an instance per object
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
    };
    Living* sLiving = nullptr;

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

    void scriptEmit(const char* pName, const NebulaValue& rValue) {
        if (sEmitDepth >= ::cEmitDepth) {
            if (!sEmitLogged) {
                sEmitLogged = true;
                nebula_log("mods: events started events %u deep (\"%s\"); the deeper ones are dropped\n", ::cEmitDepth,
                           pName);
            }
            return;
        }
        char handler[96];
        nebula_snprintf(handler, sizeof(handler), "event:%s", pName);
        sEmitDepth++;
        for (Living* l = sLiving; l != nullptr;) {
            Living* const next = l->mNext;  // a handler may end its own instance (never another's mid-walk)
            const s32 h = lumascript::findHandler(l->mProgram, handler);
            if (h >= 0) {
                (void)lumascript::run(l->mInstance, static_cast< u32 >(h), &rValue, 1);
            }
            l = next;
        }
        sEmitDepth--;
    }

    NebulaValue scriptSaved(const char* pMod, const char* pName, u8 kind) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
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
        s32 mStart;
        s32 mTouch;
        s32 mAttacked;
        s32 mAppear;
        s32 mKill;
    };

    lumascript::Instance* instanceOf(NebulaActor* pActor) {
        return sApi != nullptr ? static_cast< lumascript::Instance* >(sApi->actor_data(pActor)) : nullptr;
    }

    NebulaValue actorValue(NebulaActor* pActor) {
        return lumacore::handle(reinterpret_cast< NameObj* >(pActor));
    }

    void runHandler(NebulaActor* pActor, s32 handler, const NebulaValue* pArgs, u32 count, NebulaValue* pResult) {
        lumascript::Instance* const instance = instanceOf(pActor);
        if (instance == nullptr || handler < 0) {
            return;
        }
        const NebulaValue result = lumascript::run(instance, static_cast< u32 >(handler), pArgs, count);
        if (pResult != nullptr) {
            *pResult = result;
        }
    }

    void scriptStart(NebulaActor* pActor, void* pUser) {
        const ScriptObject* const object = static_cast< const ScriptObject* >(pUser);
        lumascript::Instance* const instance = lumacore::startInstance(object->mScript, actorValue(pActor));
        sApi->set_actor_data(pActor, instance);
        runHandler(pActor, object->mStart, nullptr, 0, nullptr);
    }

    void scriptUpdate(NebulaActor* pActor, void*) {
        lumascript::Instance* const instance = instanceOf(pActor);
        if (instance != nullptr) {
            lumascript::frame(instance);
        }
    }

    void scriptTouch(NebulaActor* pActor, void* pUser) {
        runHandler(pActor, static_cast< const ScriptObject* >(pUser)->mTouch, nullptr, 0, nullptr);
    }

    int32_t scriptAttacked(NebulaActor* pActor, int32_t how, void* pUser) {
        NebulaValue arg;
        std::memset(&arg, 0, sizeof(arg));
        arg.kind = NEBULA_KIND_INT;
        arg.u.i = how;
        NebulaValue result;
        std::memset(&result, 0, sizeof(result));
        runHandler(pActor, static_cast< const ScriptObject* >(pUser)->mAttacked, &arg, 1, &result);
        return (result.kind == NEBULA_KIND_BOOL || result.kind == NEBULA_KIND_INT) && result.u.i != 0 ? 1 : 0;
    }

    void scriptAppear(NebulaActor* pActor, void* pUser) {
        runHandler(pActor, static_cast< const ScriptObject* >(pUser)->mAppear, nullptr, 0, nullptr);
    }

    void scriptKill(NebulaActor* pActor, void* pUser) {
        runHandler(pActor, static_cast< const ScriptObject* >(pUser)->mKill, nullptr, 0, nullptr);
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
        object->mStart = lumascript::findHandler(program, "start");
        object->mTouch = lumascript::findHandler(program, "touch_player");
        object->mAttacked = lumascript::findHandler(program, "attacked");
        object->mAppear = lumascript::findHandler(program, "appear");
        object->mKill = lumascript::findHandler(program, "kill");
        // Only what the script answers: an object that ignores touches stays as plain as the game's.
        object->mClass.touch_player = object->mTouch >= 0 ? scriptTouch : nullptr;
        object->mClass.attacked_by = object->mAttacked >= 0 ? scriptAttacked : nullptr;
        object->mClass.appeared = object->mAppear >= 0 ? scriptAppear : nullptr;
        object->mClass.killed = object->mKill >= 0 ? scriptKill : nullptr;
        return sApi->register_object(&object->mClass);
    }

    void scriptsSceneEnded() {
        while (sLiving != nullptr) {
            endInstance(sLiving->mInstance);
        }
    }
}  // namespace lumacore
