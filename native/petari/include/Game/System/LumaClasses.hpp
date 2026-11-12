#pragma once
// Mods' own classes (contract classes, plan 1.4; luma.h LumaClassDef): a class derived from a game class, made by the
// game's own factory (a kernel hook on NameObjFactory::getCreator), its overrides wraps on the rows the base runs that
// run the mod's code only for its own objects, with data of its own per object. The game's vtables are never touched:
// when the mod goes, its hooks go and its objects are the base's again.

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
