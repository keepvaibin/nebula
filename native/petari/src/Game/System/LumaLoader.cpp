#include "Game/System/LumaLoader.hpp"

#include "Game/NameObj/NameObj.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHook.hpp"
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>

struct LumaMod {
    const LumaModInfo* info;
    char name[64];
    s32 order;
};

namespace luma {
    static_assert(sizeof(LumaFunction) == sizeof(Function) && sizeof(LumaCall) == sizeof(Call), "luma.h and luma_hook.h name the same things");
    static_assert(LUMA_BEFORE == Hook_Before && LUMA_AFTER == Hook_After && LUMA_WRAP == Hook_Wrap && LUMA_REPLACE == Hook_Replace,
                  "luma.h and LumaHook.hpp number the hook kinds alike");

    namespace {
        const u32 cMaxMods = 256;
        LumaMod* sMods[cMaxMods];
        u32 sModCount = 0;

        const LumaFunction* asPublic(const Function* pFunction) { return reinterpret_cast<const LumaFunction*>(pFunction); }
        const Function* asRow(const LumaFunction* pFunction) { return reinterpret_cast<const Function*>(pFunction); }

        void copyText(char* pDst, u32 size, const char* pSrc) {
            u32 i = 0;
            for (; i + 1 < size && pSrc[i] != 0; i++) {
                pDst[i] = pSrc[i];
            }
            if (size != 0) {
                pDst[i] = 0;
            }
        }

        // "name, name, ..." until the buffer is full.
        void addName(char* pMessage, u32 size, const char* pName, const char* pDetail) {
            if (pMessage == nullptr || size == 0) {
                return;
            }
            u32 at = 0;
            while (at < size && pMessage[at] != 0) {
                at++;
            }
            const char* parts[] = {at != 0 ? ", " : "", pName, pDetail != nullptr ? " " : "", pDetail != nullptr ? pDetail : ""};
            for (const char* pPart : parts) {
                for (; *pPart != 0 && at + 1 < size; pPart++) {
                    pMessage[at++] = *pPart;
                }
            }
            pMessage[at < size ? at : size - 1] = 0;
        }

        // "Class.field" split at its first dot.
        bool splitField(const char* pName, char* pClass, u32 classSize, const char** ppField) {
            u32 i = 0;
            for (; pName[i] != 0 && pName[i] != '.'; i++) {
                if (i + 1 >= classSize) {
                    return false;
                }
                pClass[i] = pName[i];
            }
            pClass[i] = 0;
            if (pName[i] != '.' || i == 0 || pName[i + 1] == 0) {
                return false;
            }
            *ppField = pName + i + 1;
            return true;
        }

        // Text as it is, after what the message holds.
        void addText(char* pMessage, u32 size, const char* pText) {
            if (pMessage == nullptr || size == 0) {
                return;
            }
            u32 at = 0;
            while (at < size && pMessage[at] != 0) {
                at++;
            }
            for (; *pText != 0 && at + 1 < size; pText++) {
                pMessage[at++] = *pText;
            }
            pMessage[at < size ? at : size - 1] = 0;
        }

        // Why a hook import was refused; the mod that holds a replace by name.
        void addHookRefusal(char* pMessage, u32 size, const LumaImport& rImport, const Function* pFunction, HookResult result) {
            addName(pMessage, size, rImport.name != nullptr ? rImport.name : "(no name)", nullptr);
            if (rImport.hook == nullptr) {
                addText(pMessage, size, " was given no hook");
            } else if (rImport.hook_kind >= Hook_KindCount) {
                addText(pMessage, size, " was asked for a kind of hook this game does not have");
            } else if (result == HookResult_Replaced) {
                const LumaMod* pOther = static_cast<const LumaMod*>(replacedBy(pFunction));
                addText(pMessage, size, " is replaced already by ");
                addText(pMessage, size, pOther != nullptr ? pOther->name : "another mod");
            } else {
                addText(pMessage, size, " takes before and after hooks only (a constructor or destructor)");
            }
        }

        void apiLog(const LumaMod* pMod, const char* pText) { nebula_log("[mod %s] %s\n", pMod != nullptr ? pMod->name : "?", pText); }

        const LumaFunction* apiFunction(const char* pName, const char* pSignature) {
            return pName != nullptr ? asPublic(findFunction(pName, pSignature, nullptr)) : nullptr;
        }

        const LumaFunction* apiFunctionAt(u32 index) { return asPublic(functionAt(index)); }

        s32 apiHook(const LumaMod* pMod, const LumaFunction* pFunction, u32 kind, LumaHook pHook, void* pUser) {
            if (pMod == nullptr) {
                return HookResult_Refused;
            }
            return addHook(asRow(pFunction), static_cast<HookKind>(kind), reinterpret_cast<HookCallback>(pHook), pUser, pMod, pMod->order);
        }

        void apiProceed(LumaCall* pCall) { proceed(reinterpret_cast<Call*>(pCall)); }

        s32 apiClassOf(const char* pClass) { return pClass != nullptr ? lumacore::findClass(pClass) : -1; }

        s32 apiSizeOf(const char* pClass) {
            const s32 id = apiClassOf(pClass);
            return id < 0 ? -1 : static_cast<s32>(lumacore::classRow(static_cast<u16>(id)).size);
        }

        s32 apiField(const char* pClass, const char* pField, const char* /*pType*/) {
            const s32 id = apiClassOf(pClass);
            if (id < 0 || pField == nullptr) {
                return -1;
            }
            // A field of a base is found on the base it belongs to.
            for (u16 at = static_cast<u16>(id);; at = lumacore::classRow(at).base) {
                const lumacore::FieldRow* pRow = lumacore::findField(at, pField);
                if (pRow != nullptr) {
                    return static_cast<s32>(pRow->offset);
                }
                if (at == 0 || lumacore::classRow(at).base == at) {
                    return -1;
                }
            }
        }

        s32 apiIsA(const void* pObject, s32 classNumber) {
            if (pObject == nullptr || classNumber < 0) {
                return 0;
            }
            return lumacore::isA(lumacore::classOf(static_cast<const NameObj*>(pObject)), static_cast<u16>(classNumber)) ? 1 : 0;
        }

        // Services come from the host (luma_set_service_finder), which asks its own services and its extensions.
        LumaServiceFinder sServiceFinder = nullptr;

        const void* apiService(const char* pName, u32 major, u32 minor) {
            return pName != nullptr && sServiceFinder != nullptr ? sServiceFinder(pName, major, minor) : nullptr;
        }

        const LumaCore sCore = {
            sizeof(LumaCore), LUMA_API, apiLog, apiFunction, apiFunctionAt, apiHook, apiProceed, apiField, apiSizeOf, apiClassOf, apiIsA, apiService,
        };

        // Resolves one import into `pSlotValue` (a pointer or a number). False: the game has no such name.
        bool resolve(const LumaImport& rImport, const Function** ppFunction, s32* pNumber, const char** ppWhy) {
            *ppWhy = nullptr;
            if (rImport.name == nullptr) {
                return false;
            }
            switch (rImport.kind) {
            case LUMA_IMPORT_FUNCTION:
            case LUMA_IMPORT_HOOK: {
                FindResult found;
                *ppFunction = findFunction(rImport.name, rImport.signature, &found);
                if (found == Find_OtherSignature) {
                    *ppWhy = "(its signature differs)";
                } else if (found == Find_SeveralSignatures) {
                    *ppWhy = "(several; a signature is needed)";
                }
                return *ppFunction != nullptr;
            }
            case LUMA_IMPORT_FIELD: {
                char className[96];
                const char* pField = nullptr;
                if (!splitField(rImport.name, className, sizeof(className), &pField)) {
                    return false;
                }
                *pNumber = apiField(className, pField, rImport.signature);
                return *pNumber >= 0;
            }
            case LUMA_IMPORT_SIZE:
                *pNumber = apiSizeOf(rImport.name);
                return *pNumber >= 0;
            case LUMA_IMPORT_CLASS:
                *pNumber = apiClassOf(rImport.name);
                return *pNumber >= 0;
            default:
                *ppWhy = "(a kind of import this game does not know)";
                return false;
            }
        }
    }

    const LumaCore* core() { return &sCore; }

    bool loadMod(const LumaModInfo* pInfo, const char* pName, s32 order, char* pMessage, u32 messageSize) {
        if (pMessage != nullptr && messageSize != 0) {
            pMessage[0] = 0;
        }
        // Each refusal says which it is, as a whole sentence the host prints as it is.
        if (pInfo == nullptr) {
            addText(pMessage, messageSize, "its luma_mod is empty");
            return false;
        }
        if (pInfo->api > LUMA_API) {
            addText(pMessage, messageSize, "it was made for a newer LumaCore than this game has");
            return false;
        }
        if (pInfo->size < sizeof(LumaModInfo)) {
            addText(pMessage, messageSize, "its luma_mod is shorter than luma.h says (it was not built with luma.h)");
            return false;
        }
        if (pInfo->import_count != 0 && pInfo->imports == nullptr) {
            addText(pMessage, messageSize, "its luma_mod lists imports but gives none");
            return false;
        }
        if (sModCount == cMaxMods) {
            addText(pMessage, messageSize, "more than 256 LumaCore mods are on");
            return false;
        }
        // Every name first: a mod is bound whole or not at all.
        bool whole = true;
        for (u32 i = 0; i < pInfo->import_count; i++) {
            const LumaImport& rImport = pInfo->imports[i];
            const Function* pFunction = nullptr;
            s32 number = -1;
            const char* pWhy = nullptr;
            if (!resolve(rImport, &pFunction, &number, &pWhy) && (rImport.flags & LUMA_OPTIONAL) == 0) {
                if (whole) {
                    addText(pMessage, messageSize, "the game has no ");
                    addText(pMessage, messageSize, rImport.name != nullptr ? rImport.name : "(no name)");
                    if (pWhy != nullptr) {
                        addText(pMessage, messageSize, " ");
                        addText(pMessage, messageSize, pWhy);
                    }
                    whole = false;
                    continue;
                }
                addName(pMessage, messageSize, rImport.name != nullptr ? rImport.name : "(no name)", pWhy);
                whole = false;
            }
        }
        if (!whole) {
            return false;
        }

        LumaMod* pMod = new LumaMod();
        pMod->info = pInfo;
        pMod->order = order;
        copyText(pMod->name, sizeof(pMod->name), pName != nullptr ? pName : "");
        for (u32 i = 0; i < pInfo->import_count; i++) {
            const LumaImport& rImport = pInfo->imports[i];
            const Function* pFunction = nullptr;
            s32 number = -1;
            const char* pWhy = nullptr;
            const bool found = resolve(rImport, &pFunction, &number, &pWhy);
            if (rImport.kind == LUMA_IMPORT_HOOK) {
                const HookResult result = found ? addHook(pFunction, static_cast<HookKind>(rImport.hook_kind), reinterpret_cast<HookCallback>(rImport.hook),
                                                          rImport.user, pMod, order)
                                                : HookResult_Added;
                if (result != HookResult_Added) {
                    addHookRefusal(pMessage, messageSize, rImport, pFunction, result);
                    whole = false;
                }
            } else if (rImport.slot != nullptr) {
                if (rImport.kind == LUMA_IMPORT_FUNCTION) {
                    *static_cast<const LumaFunction**>(rImport.slot) = asPublic(pFunction);
                } else {
                    *static_cast<s32*>(rImport.slot) = found ? number : -1;
                }
            }
        }
        if (whole && pInfo->start != nullptr) {
            const int32_t refused = pInfo->start(&sCore, pMod);
            if (refused != 0) {
                char number[16];
                nebula_snprintf(number, sizeof(number), "%d", static_cast<int>(refused));
                addText(pMessage, messageSize, "its start refused to run (it returned ");
                addText(pMessage, messageSize, number);
                addText(pMessage, messageSize, ")");
                whole = false;
            }
        }
        if (!whole) {
            removeHooks(pMod);
            delete pMod;
            return false;
        }
        sMods[sModCount++] = pMod;
        return true;
    }

    void unloadMod(const LumaModInfo* pInfo) {
        for (u32 i = 0; i < sModCount; i++) {
            if (sMods[i]->info == pInfo) {
                if (pInfo->stop != nullptr) {
                    pInfo->stop();
                }
                removeHooks(sMods[i]);
                delete sMods[i];
                sMods[i] = sMods[--sModCount];
                return;
            }
        }
    }
}

extern "C" {
NEBULA_GAME_API int32_t luma_load_mod(const LumaModInfo* info, const char* name, int32_t order, char* message, uint32_t message_size) {
    return luma::loadMod(info, name, order, message, message_size) ? 1 : 0;
}

NEBULA_GAME_API void luma_unload_mod(const LumaModInfo* info) {
    luma::unloadMod(info);
}

NEBULA_GAME_API const LumaCore* luma_core(void) {
    return luma::core();
}

NEBULA_GAME_API void luma_set_service_finder(LumaServiceFinder finder) {
    luma::sServiceFinder = finder;
}
}
