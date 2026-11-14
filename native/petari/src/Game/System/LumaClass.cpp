// Mod classes on the game's actors (LumaClass.hpp).
#include "Game/System/LumaClass.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHost.hpp"
#include "Game/System/LumaScript.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_module.h>
#include <cstring>
#include <new>

namespace {
    // One mod's row for a class name: a script, or a plugin's hooks.
    struct ModClassRow {
        const char* mName;
        const NebulaPlugin* mMod;
        s32 mScript;  // -1: hooks
        NebulaClassHooks mHooks;
    };

    // The rows, in the order the mods load; they live for the run (root arena).
    ModClassRow* sRows = nullptr;
    u32 sRoom = 0;

    char* copyText(const char* pText) {
        const u32 length = static_cast< u32 >(std::strlen(pText));
        char* copy = new (NebulaArena::root(), 0) char[length + 1];
        std::memcpy(copy, pText, length + 1);
        return copy;
    }

    ModClassRow* addRow(const NebulaPlugin* pMod, const char* pName) {
        if (pName == nullptr || pName[0] == '\0') {
            return nullptr;
        }
        if (lumacore::sClassRowCount == sRoom) {
            const u32 room = sRoom == 0 ? 8 : sRoom * 2;
            ModClassRow* rows = new (NebulaArena::root(), 0) ModClassRow[room];
            for (u32 i = 0; i < lumacore::sClassRowCount; i++) {
                rows[i] = sRows[i];
            }
            delete[] sRows;
            sRows = rows;
            sRoom = room;
        }
        ModClassRow& row = sRows[lumacore::sClassRowCount++];
        std::memset(&row, 0, sizeof(row));
        row.mName = copyText(pName);
        row.mMod = pMod;
        row.mScript = -1;
        return &row;
    }

    NebulaValue none() {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        return v;
    }

    NebulaActor* actorHandle(LiveActor* pActor) {
        return reinterpret_cast< NebulaActor* >(pActor);
    }

    bool answered(const NebulaValue& rResult, bool* pAnswer) {
        if (rResult.kind != NEBULA_KIND_BOOL && rResult.kind != NEBULA_KIND_INT) {
            return false;
        }
        *pAnswer = rResult.u.i != 0;
        return true;
    }
}  // namespace

namespace lumacore {
    struct ModActor {
        const ModClassRow* mRow;
        lumascript::Instance* mInstance;  // a script row's own, for this actor
        ModActor* mNext;
    };

    u32 sClassRowCount = 0;

    s32 addClassScript(const NebulaPlugin* pMod, const char* pName, s32 script) {
        if (lumacore::script(script) == nullptr) {
            return -1;
        }
        ModClassRow* const row = addRow(pMod, pName);
        if (row == nullptr) {
            return -1;
        }
        row->mScript = script;
        return 0;
    }

    s32 addClassHooks(const NebulaPlugin* pMod, const char* pName, const NebulaClassHooks* pHooks) {
        if (pHooks == nullptr || pHooks->size < sizeof(uint32_t) + sizeof(void*)) {
            return -1;
        }
        ModClassRow* const row = addRow(pMod, pName);
        if (row == nullptr) {
            return -1;
        }
        // An older plugin's smaller struct: the hooks past its end stay null.
        const u32 size = pHooks->size < sizeof(NebulaClassHooks) ? pHooks->size : static_cast< u32 >(sizeof(NebulaClassHooks));
        std::memcpy(&row->mHooks, pHooks, size);
        row->mHooks.size = size;
        return 0;
    }

    void attachClass(NameObj* pObj, const char* pPlacedName) {
        if (pObj == nullptr || pPlacedName == nullptr || !isA(classOf(pObj), kClassId< LiveActor >)) {
            return;
        }
        LiveActor* const actor = static_cast< LiveActor* >(pObj);
        // The game class's rows first (a mod's "ChargeGoomba" made like "Kuribo" is a Kuribo), then its own name's.
        const char* const like = NebulaMods::objectLike(pPlacedName);
        const char* const names[] = {like, like != pPlacedName && !MR::isEqualStringCase(like, pPlacedName) ? pPlacedName : nullptr};
        ModActor** tail = &actor->mModActor;
        while (*tail != nullptr) {
            tail = &(*tail)->mNext;
        }
        ModActor** const added = tail;
        for (const char* name : names) {
            if (name == nullptr) {
                continue;
            }
            for (u32 i = 0; i < sClassRowCount; i++) {
                const ModClassRow& row = sRows[i];
                if (!MR::isEqualStringCase(row.mName, name)) {
                    continue;
                }
                ModActor* const node = new ModActor();
                node->mRow = &row;
                node->mInstance = row.mScript >= 0 ? startInstance(row.mScript, handle(actor)) : nullptr;
                node->mNext = nullptr;
                *tail = node;
                tail = &node->mNext;
            }
        }
        for (ModActor* m = *added; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                (void)lumascript::dispatch(m->mInstance, lumascript::Event_Start, -1, nullptr, 0, nullptr);
            } else if (m->mRow->mHooks.start != nullptr) {
                m->mRow->mHooks.start(actorHandle(actor), m->mRow->mHooks.user);
            }
        }
    }

    bool classControl(LiveActor* pActor) {
        bool skip = false;
        for (ModActor* m = pActor->mModActor; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                lumascript::frame(m->mInstance);
                NebulaValue result = none();
                bool answer = false;
                if (lumascript::dispatch(m->mInstance, lumascript::Event_Control, -1, nullptr, 0, &result) && answered(result, &answer)) {
                    skip = skip || answer;
                }
            } else if (m->mRow->mHooks.control != nullptr) {
                skip = m->mRow->mHooks.control(actorHandle(pActor), m->mRow->mHooks.user) != 0 || skip;
            }
        }
        return skip;
    }

    void classAppear(LiveActor* pActor) {
        for (ModActor* m = pActor->mModActor; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                (void)lumascript::dispatch(m->mInstance, lumascript::Event_Appear, -1, nullptr, 0, nullptr);
            } else if (m->mRow->mHooks.appeared != nullptr) {
                m->mRow->mHooks.appeared(actorHandle(pActor), m->mRow->mHooks.user);
            }
        }
    }

    void classKill(LiveActor* pActor) {
        for (ModActor* m = pActor->mModActor; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                (void)lumascript::dispatch(m->mInstance, lumascript::Event_Kill, -1, nullptr, 0, nullptr);
            } else if (m->mRow->mHooks.killed != nullptr) {
                m->mRow->mHooks.killed(actorHandle(pActor), m->mRow->mHooks.user);
            }
        }
    }

    s32 classMessage(LiveActor* pActor, u32 message, HitSensor* pSender) {
        LiveActor* const from = pSender != nullptr ? pSender->mHost : nullptr;
        for (ModActor* m = pActor->mModActor; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                // "message:<n>" (from) for this message, else "message" (number, from).
                NebulaValue args[2] = {none(), from != nullptr ? handle(from) : none()};
                args[0].kind = NEBULA_KIND_INT;
                args[0].u.i = static_cast< s64 >(message);
                NebulaValue result = none();
                bool answer = false;
                if (lumascript::dispatch(m->mInstance, lumascript::Event_Message, static_cast< s32 >(message), &args[1], 1, &result) &&
                    answered(result, &answer)) {
                    return answer ? 1 : 0;
                }
                result = none();
                if (lumascript::dispatch(m->mInstance, lumascript::Event_Message, -1, args, 2, &result) && answered(result, &answer)) {
                    return answer ? 1 : 0;
                }
            } else if (m->mRow->mHooks.message != nullptr) {
                const int32_t answer = m->mRow->mHooks.message(actorHandle(pActor), static_cast< int32_t >(message), from != nullptr ? actorHandle(from) : nullptr,
                                             m->mRow->mHooks.user);
                if (answer >= 0) {
                    return answer != 0 ? 1 : 0;
                }
            }
        }
        return -1;
    }

    bool classTouch(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver) {
        LiveActor* const other = pReceiver != nullptr ? pReceiver->mHost : nullptr;
        bool skip = false;
        for (ModActor* m = pActor->mModActor; m != nullptr; m = m->mNext) {
            if (m->mInstance != nullptr) {
                const NebulaValue args[2] = {other != nullptr ? handle(other) : none(), handle(pSender)};
                NebulaValue result = none();
                bool answer = false;
                if (lumascript::dispatch(m->mInstance, lumascript::Event_TouchActor, -1, args, 2, &result) && answered(result, &answer)) {
                    skip = skip || answer;
                }
            } else if (m->mRow->mHooks.touch != nullptr) {
                skip = m->mRow->mHooks.touch(actorHandle(pActor), other != nullptr ? actorHandle(other) : nullptr, m->mRow->mHooks.user) != 0 || skip;
            }
        }
        return skip;
    }
}  // namespace lumacore
