#pragma once

// The mods' scripts in the game (LumaHost.cpp): loaded by mod and path, instances on objects, events, saved values.

#include <nebula_mod_api.h>
#include <nebula_types.h>

namespace lumascript {
    class Program;
    class Instance;
}  // namespace lumascript

namespace lumacore {
    // The plugin API as the host completed it (its store, log and format): set once, before any script loads.
    void setApi(const NebulaModApi* pApi);
    const NebulaModApi* api();
    // The mod whose script runs now (null between scripts): the toolkit's nebula.* calls that take the calling mod.
    const NebulaPlugin* runningMod();
    // A script event's id by name ("gumbo_paid"), the same for every script: resolved when scripts load.
    u32 eventId(const char* pName);
    // A game event by its script name ("stage_start", "coin": NEBULA_MOD_EVENT_* lower-case), or -1. "frame" is the
    // scripts' own and not one of these.
    s32 gameEvent(const char* pName);
    // A mod's script file by its store path ("/LumaScripts/<mod>/<name>.lumascript"): its index, or -1 and why.
    s32 loadScript(const NebulaPlugin* pMod, const char* pPath, char* pMessage, u32 messageSize);
    lumascript::Program* script(s32 index);
    const NebulaPlugin* scriptMod(const lumascript::Program* pProgram);
    void forEachScriptProgram(void (*pDo)(lumascript::Program*));
    // An instance of a script on `self`, known to events until endInstance.
    lumascript::Instance* startInstance(s32 script, const NebulaValue& rSelf);
    void endInstance(lumascript::Instance* pInstance);
    // Every instance whose script answers "event:<name>" (eventId) runs it with the value.
    void scriptEmit(u32 event, const NebulaValue& rValue);
    // Values a script keeps in the save (the mod's store).
    NebulaValue scriptSaved(const char* pMod, const char* pName, u8 kind);
    void setScriptSaved(const char* pMod, const char* pName, const NebulaValue& rValue);
    // An object a script runs (luma.json "objects" with "script"): its shape as a plugin's, its handlers "start",
    // "frame", "touch_player", "attacked" (with how; returns whether it hit), "appear", "kill", "message". 0: added.
    s32 addScriptObject(const NebulaObjectClass* pShape, s32 script);
    // A mod-wide script (luma.json "scripts"): one instance for the whole run, self none. Its "frame" runs every frame
    // (menus too); it answers the game's events and other scripts' events. 0: started.
    s32 addModScript(s32 script);
    // The scene ends: every object's instance goes (handles into it read as none from here on); mod-wide ones stay.
    void scriptsSceneEnded();
    // A registry row by "<kind>/<name>": its index, or -1.
    s32 registryIndex(const char* pName);
}  // namespace lumacore
