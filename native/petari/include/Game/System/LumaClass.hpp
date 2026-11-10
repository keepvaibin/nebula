#pragma once

// Mod classes on the game's actors (agents/LUMACORE-PLAN.md C4, C5, 8.6): what runs on every actor placed by a name, a
// game class ("Kuribo") or a mod's made like one (luma.json "object_classes"). Scripts (every platform) and plugin hooks
// (NebulaClassHooks) answer the same dispatch points: after the class's init, each frame before its control (and in
// place of it), appear, kill, every message it receives and every sensor of it touching another.
// An actor no mod class names has a null LiveActor::mModActor: each dispatch point is one test of it.

#include <nebula_mod_api.h>
#include <nebula_types.h>

class HitSensor;
class LiveActor;
class NameObj;

namespace lumacore {
    struct ModActor;

    // Only while plugins start. A script (an index from loadScript) or hooks for every actor placed by `pName`.
    // Several mods may name one class: each runs, in the order the mods load. 0: done.
    s32 addClassScript(const NebulaPlugin* pMod, const char* pName, s32 script);
    s32 addClassHooks(const NebulaPlugin* pMod, const char* pName, const NebulaClassHooks* pHooks);

    // Whether any mod names a class: placement asks nothing more when none does.
    extern u32 sClassRowCount;

    // A placed object made and initialised (PlacementInfoOrdered): its mod class rows by the placed name and the name
    // it is made like, the scripts started and "start" run.
    void attachClass(NameObj* pObj, const char* pPlacedName);

    // The dispatch points (the actor's mModActor is set).
    // Each frame before the class's control: the scripts' waits, states and "frame", then "control"; true when one
    // took the class's place (its control is skipped this frame).
    bool classControl(LiveActor* pActor);
    void classAppear(LiveActor* pActor);
    void classKill(LiveActor* pActor);
    // A message to the actor: -1 when no mod answered (the class reacts), else what the actor answers (the class's
    // own reaction skipped).
    s32 classMessage(LiveActor* pActor, u32 message, HitSensor* pSender);
    // One of the actor's sensors touching another's: true when a mod took it (the class's attackSensor is skipped).
    bool classTouch(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver);
}  // namespace lumacore
