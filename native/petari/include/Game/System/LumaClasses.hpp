#pragma once
// Mods' own classes (contract classes, plan 1.4; luma.h LumaClassDef): a class derived from a game class, made by the
// game's own factory (a kernel hook on NameObjFactory::getCreator), with data of its own per object. Its overrides are
// slots of a table of its own: a copy of the base's table at the object's start (prefix and RTTI too), whose overridden
// slots hold the build's entry for that virtual (gen_types.py slot_code); `made` sees the object with that table, and
// a virtual call on it runs the mod's hook, whose proceed runs the base's code. The game's own tables are never
// written: when the mod goes, its objects get the base's table back.

#include <luma.h>
#include <luma_types.h>
#include <nebula_types.h>

namespace luma {
    namespace classes {
        // LumaCore.define_class: the class's number (classCount() + its row), or luma.h's -1 to -5, said in the log.
        // On the game thread.
        s32 define(const LumaMod* pMod, const LumaClassDef* pDef);
        // LumaCore.class_data: the object's data when it is one of class `number`'s, else null. Any thread.
        void* data(const void* pObject, s32 number);
        // The mod class of an object (from the object or any polymorphic subobject): its number, or -1. Any thread.
        s32 classOfObject(const void* pObject);
        // The whole object when pObject is one of class `number`'s, else null. Any thread.
        const void* asModClass(const void* pObject, s32 number);
        // A live mod class's number by its name, or -1.
        s32 numberOf(const char* pName);
        // LumaCore.alias_class: 0, or luma.h's -1, -2, -3 or -5, said in the log. On the game thread.
        s32 alias(const LumaMod* pMod, const char* pName, const char* pClass, const s32* pArgs);
        // While it lives, the placement row (pTable, entry) is initialising an object placed as `pName`: when that name
        // is a live alias, the row's Obj_arg reads on this thread give the alias's arguments (placementArg). Nests.
        class AliasPlacing {
        public:
            AliasPlacing(const char* pName, const void* pTable, s32 entry);
            ~AliasPlacing();
            AliasPlacing(const AliasPlacing&) = delete;
            AliasPlacing& operator=(const AliasPlacing&) = delete;

            const void* mTable;
            s32 mEntry;
            s32 mArgs[8];
            AliasPlacing* mOuter;
            bool mSet;
        };
        // Obj_arg`column`'s value for the row (pTable, entry) as the alias placing it gives it: true and *pOut, or false
        // (no alias places that row on this thread, the column is no Obj_arg0 to 7, or the alias leaves it -1).
        bool placementArg(const void* pTable, s32 entry, const char* pColumn, s32* pOut);
        // A number at or above the game's classes (classCount()): a mod class's, live or gone.
        bool isModNumber(s32 number);
        // The mod is going (unloadMod, a refused start), after its hooks: its classes end and their objects are plain
        // objects of the base again. Their data is the mod's memory, freed with it after the grace. Game thread.
        void modGone(const LumaMod* pMod);
        // The scene's objects are gone (GameSystemSceneController::destroyScene): every mod object is forgotten and its
        // data freed. Game thread.
        void sceneGone();
    }
}
