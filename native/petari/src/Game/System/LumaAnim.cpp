#include "Game/System/LumaAnim.hpp"

#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/System/LumaContentModel.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Table.hpp"
#include <luma.h>
#include <luma_anim.h>
#include <luma_modlog.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <cmath>
#include <cstring>

// The anim service (plan 3.6 run time; contract animation 7). Game thread throughout: the service's calls, the frame,
// the provider; NebulaArena::destroy is the scene end's, on the game thread as well.
//
// A clip's own events (1.1, animation 7.3): G's cook puts the rows of <clip>.anim.json beside the clip as <clip>.events,
// a table (frame int, event text) in the model's archive. A play keeps the table's place in the archive (the actor's
// resources outlive the play: the play ends with the actor or its scene) and gives a row as the mark mark_count + row,
// its name copied into the event.
//
// A play starts the clip as the game's own code does (MR::startBck and the material starts): the actor's ModelManager
// runs it in the actor's movement, its effect and sound keepers follow the joint clip as for the game's clips. The
// service only watches the leading kind's frame control at the frame's start (after last frame's movement): a mark is
// given when that frame moved onto or past it, the end when a once clip's control says it ended. A paused game moves
// no frame: no mark, no end.
//
// No cost with no mod: frame() and released() are one load each while no play is going.

namespace luma {
    namespace anim {
        namespace {
            const u32 kMostOpen = LUMA_ANIM_MOST_PLAYS * 8u;  // across mods
            const u32 kMostEvents = kMostOpen * 4u;          // marks and ends waiting or being given, across mods
            const u32 cMissingAnimationCode = 627;            // content's block (references 6)
            const u32 cEventPastEndCode = 670;
            const u32 kClipMost = 128;                        // a clip name's bytes with its 0

            // The kinds of clip of one name a model can have, in the order the first present leads.
            enum Kind : u8 { kBck, kBtk, kBrk, kBpk, kBtp, kBva, kKinds };
            const char* const cKindNames[kKinds] = {"bck", "btk", "brk", "bpk", "btp", "bva"};

            struct Play {
                LumaHandle handle;  // 0: a free slot
                const LumaMod* mod;
                LiveActor* actor;
                LumaHandle model;   // the content.model handle it plays on (0: an actor of the game)
                u8 kinds;           // bit per Kind
                u8 lead;
                bool loops;         // never ends by itself
                f32 seen;           // the lead's frame at the last look
                u32 markCount;
                f32 marks[LUMA_ANIM_MOST_MARKS];
                const void* events;  // the clip's cooked event table in its archive, or null
                u32 eventRows;
                s32 eventFrame;      // the table's columns
                s32 eventName;
                LumaEventFn fn;
                void* user;
                char clip[kClipMost];
            };

            // An event on its way: its own copy of what it says (its play may end meanwhile). Busy from its post to the
            // start of its deliver, or its drop.
            struct Event {
                LumaEventNode node;
                bool busy;
                u32 kind;
                s32 status;
                LumaHandle play;
                u64 value;
                LumaEventFn fn;
                void* user;
                u32 nameSize;  // an event row's mark: its name's bytes (no NUL)
                char name[LUMA_ANIM_EVENT_NAME_MOST + 1];
            };

            const LumaServiceKernel* sKernel = nullptr;
            Play sPlays[kMostOpen];
            u32 sHigh = 0;  // slots below are in use or were
            u32 sLive = 0;  // plays going
            LumaHandle sNextHandle = 0;
            Event sEvents[kMostEvents];

            // ---- Calls ----

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1,
                        const char* pArg2) {
                const char* args[3] = {pArg0, pArg1, pArg2};
                u32 count = 0;
                while (count < 3 && args[count] != nullptr) {
                    count++;
                }
                sKernel->report(pMod, pWhere, code, args, count);
            }

            // A CONTENT entry of the mod (references 6).
            void sayContent(const LumaMod* pMod, u32 code, const char* pRow, u32 column, const char* pName0,
                            const char* pValue0, const char* pName1, const char* pValue1) {
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CONTENT;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = code;
                entry.mod = sKernel->mod_id(pMod);
                entry.file = "";
                entry.column = column;
                entry.names[0] = pName0;
                entry.values[0] = pValue0;
                entry.count = 1;
                if (pName1 != nullptr) {
                    entry.names[1] = pName1;
                    entry.values[1] = pValue1;
                    entry.count = 2;
                }
                (void)luma::modlog::add(entry, pRow);
            }

            s32 enter(const LumaMod* pMod, const char* pWhere) {
                if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                    sKernel->unknown_mod(pWhere);
                    return LUMA_E_ARGUMENT;
                }
                if (sKernel->mod_live(pMod) == 0) {
                    return LUMA_E_CANCELLED;
                }
                if (sKernel->game_thread() == 0) {
                    report(pMod, pWhere, 100u, nullptr, nullptr, nullptr);
                    return LUMA_E_THREAD;
                }
                return LUMA_OK;
            }

            const char* modelNameOf(const LiveActor* pActor) {
                if (const ResourceHolder* pHolder = MR::getModelResourceHolder(pActor); pHolder != nullptr) {
                    if (const char* pModel = pHolder->getModelName(); pModel != nullptr) {
                        return pModel;
                    }
                }
                return lumacore::classRow(lumacore::classOf(pActor)).name;
            }

            Play* playOf(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < sHigh; i++) {
                    if (sPlays[i].handle == handle) {
                        return sPlays[i].mod == pMod ? &sPlays[i] : nullptr;
                    }
                }
                return nullptr;
            }

            // ---- Kinds ----

            bool exists(const LiveActor* pActor, u32 kind, const char* pClip) {
                switch (kind) {
                case kBck:
                    return MR::isExistBck(pActor, pClip);
                case kBtk:
                    return MR::isExistBtk(pActor, pClip);
                case kBrk:
                    return MR::isExistBrk(pActor, pClip);
                case kBpk:
                    return MR::isExistBpk(pActor, pClip);
                case kBtp:
                    return MR::isExistBtp(pActor, pClip);
                default:
                    return MR::isExistBva(pActor, pClip);
                }
            }

            // Only for a kind the model has (another would make an empty player).
            FrameCtrl* ctrlOf(const LiveActor* pActor, u32 kind) {
                const ModelManager* const pManager = pActor->mModelManager;
                switch (kind) {
                case kBck:
                    return pManager->getBckCtrl();
                case kBtk:
                    return pManager->getBtkCtrl();
                case kBrk:
                    return pManager->getBrkCtrl();
                case kBpk:
                    return pManager->getBpkCtrl();
                case kBtp:
                    return pManager->getBtpCtrl();
                default:
                    return pManager->getBvaCtrl();
                }
            }

            // The kind still runs this clip (stopped or ended on its last frame counts: nothing else was started).
            bool runs(const LiveActor* pActor, u32 kind, const char* pClip) {
                const ModelManager* const pManager = pActor->mModelManager;
                switch (kind) {
                case kBck:
                    return pManager->mXanimePlayer != nullptr && pManager->mXanimePlayer->isRun(pClip);
                case kBtk:
                    return pManager->isBtkPlaying(pClip);
                case kBrk:
                    return pManager->isBrkPlaying(pClip);
                case kBpk:
                    return pManager->isBpkPlaying(pClip);
                case kBtp:
                    return pManager->isBtpPlaying(pClip);
                default:
                    return pManager->isBvaPlaying(pClip);
                }
            }

            bool ended(const LiveActor* pActor, u32 kind) {
                if (kind == kBck) {
                    return pActor->mModelManager->isBckStopped();
                }
                return ctrlOf(pActor, kind)->checkState(1);
            }

            void start(const LiveActor* pActor, u32 kind, const char* pClip, s32 blend) {
                switch (kind) {
                case kBck:
                    if (blend == LUMA_ANIM_BLEND_CLIP) {
                        MR::startBck(pActor, pClip, nullptr);
                    } else {
                        MR::startBckWithInterpole(pActor, pClip, blend < 0 ? 0 : blend);
                    }
                    break;
                case kBtk:
                    MR::startBtk(pActor, pClip);
                    break;
                case kBrk:
                    MR::startBrk(pActor, pClip);
                    break;
                case kBpk:
                    MR::startBpk(pActor, pClip);
                    break;
                case kBtp:
                    MR::startBtp(pActor, pClip);
                    break;
                default:
                    MR::startBva(pActor, pClip);
                    break;
                }
            }

            // ---- Events ----

            void deliverEvent(LumaEventNode* pNode) {
                Event* const pEvent = reinterpret_cast< Event* >(pNode);
                const LumaMod* const pMod = pNode->mod;
                LumaEvent event = {};
                event.size = sizeof(event);
                event.kind = pEvent->kind;
                event.status = pEvent->status;
                event.handle = pEvent->play;
                event.value = pEvent->value;
                // The name is the call's own copy: the slot is free during the call and may take another event.
                char name[LUMA_ANIM_EVENT_NAME_MOST + 1];
                if (pEvent->nameSize != 0) {
                    memcpy(name, pEvent->name, pEvent->nameSize);
                    name[pEvent->nameSize] = 0;
                    event.data = name;
                    event.data_size = pEvent->nameSize;
                }
                const LumaEventFn fn = pEvent->fn;
                void* const pUser = pEvent->user;
                pEvent->busy = false;  // free again before the mod's function, which may play again
                (void)sKernel->call(pMod, LUMA_ANIM_NAME, fn, pUser, &event);
            }

            void dropEvent(LumaEventNode* pNode) {
                reinterpret_cast< Event* >(pNode)->busy = false;
            }

            void post(const Play& rPlay, u32 kind, s32 status, u64 value, const char* pName = nullptr) {
                if (rPlay.fn == nullptr) {
                    return;
                }
                for (u32 i = 0; i < kMostEvents; i++) {
                    Event& rEvent = sEvents[i];
                    if (rEvent.busy) {
                        continue;
                    }
                    rEvent.busy = true;
                    rEvent.node.mod = rPlay.mod;
                    rEvent.node.deliver = deliverEvent;
                    rEvent.node.drop = dropEvent;
                    rEvent.kind = kind;
                    rEvent.status = status;
                    rEvent.play = rPlay.handle;
                    rEvent.value = value;
                    rEvent.fn = rPlay.fn;
                    rEvent.user = rPlay.user;
                    rEvent.nameSize = 0;
                    if (pName != nullptr) {
                        while (rEvent.nameSize < LUMA_ANIM_EVENT_NAME_MOST && pName[rEvent.nameSize] != 0) {
                            rEvent.name[rEvent.nameSize] = pName[rEvent.nameSize];
                            rEvent.nameSize++;
                        }
                    }
                    if (sKernel->post(&rEvent.node) != LUMA_OK) {
                        rEvent.busy = false;  // the mod is gone: no one to tell
                    }
                    return;
                }
                nebula_log("anim: no room for an event of %s; it is not given\n", sKernel->mod_id(rPlay.mod));
            }

            // `pName`: an event row's name (null: one of the mod's own marks).
            void mark(const Play& rPlay, u32 index, f32 frame, const char* pName = nullptr) {
                post(rPlay, LUMA_ANIM_EVENT_MARK, LUMA_OK, index, pName);
                if (luma::modlog::facts()) {
                    char key[64];
                    nebula_snprintf(key, sizeof(key), "anim.mark.%llu.%u", static_cast< unsigned long long >(rPlay.handle), index);
                    char quoted[2 * LUMA_ANIM_EVENT_NAME_MOST + 8];
                    luma::modlog::jsonString(quoted, sizeof(quoted), pName != nullptr ? pName : "");
                    char json[224];
                    if (pName != nullptr) {
                        nebula_snprintf(json, sizeof(json), "{\"frame\":%.3f,\"event\":%s}", static_cast< double >(frame), quoted);
                    } else {
                        nebula_snprintf(json, sizeof(json), "{\"frame\":%.3f}", static_cast< double >(frame));
                    }
                    luma::modlog::fact(key, json);
                }
            }

            // The clip's cooked events in the actor's model archive: the table, its rows (at most
            // LUMA_ANIM_MOST_EVENT_ROWS) and its columns; null with none, or with a file that is not the cook's table
            // (named in the log once a play).
            const void* findEvents(const LumaMod* pMod, const LiveActor* pActor, const char* pClip, u32* pRows,
                                   s32* pFrame, s32* pName) {
                const ResourceHolder* const pHolder = MR::getModelResourceHolder(pActor);
                if (pHolder == nullptr) {
                    return nullptr;
                }
                char file[kClipMost + 8];
                nebula_snprintf(file, sizeof(file), "%s.events", pClip);
                const void* const pFile = pHolder->findFile(file);
                if (pFile == nullptr) {
                    return nullptr;
                }
                if (memcmp(pFile, "NTBL", 4) != 0) {
                    nebula_log("anim: %s's %s is not a cooked table; its events are not given\n", sKernel->mod_id(pMod), file);
                    return nullptr;
                }
                Table table;
                table.attach(pFile);
                const s32 frameColumn = table.searchItemInfo("frame");
                const s32 nameColumn = table.searchItemInfo("event");
                if (frameColumn < 0 || nameColumn < 0 || table.getColumnKind("frame") != TableKind_Int ||
                    table.getColumnKind("event") != TableKind_Text) {
                    nebula_log("anim: %s's %s lacks its frame and event columns; its events are not given\n",
                               sKernel->mod_id(pMod), file);
                    return nullptr;
                }
                u32 rows = static_cast< u32 >(table.getNumEntries());
                if (rows > LUMA_ANIM_MOST_EVENT_ROWS) {
                    nebula_log("anim: %s's %s has %u rows; the first %u are given\n", sKernel->mod_id(pMod), file, rows,
                               LUMA_ANIM_MOST_EVENT_ROWS);
                    rows = LUMA_ANIM_MOST_EVENT_ROWS;
                }
                *pRows = rows;
                *pFrame = frameColumn;
                *pName = nameColumn;
                return rows != 0 ? pFile : nullptr;
            }

            // ---- Ends ----

            const char* whyName(u32 why) {
                switch (why) {
                case LUMA_ANIM_ENDED:
                    return "end";
                case LUMA_ANIM_STOPPED:
                    return "stop";
                case LUMA_ANIM_REPLACED:
                    return "replaced";
                case LUMA_ANIM_ACTOR:
                    return "actor";
                case LUMA_ANIM_SCENE:
                    return "scene";
                default:
                    return "mod";
                }
            }

            // The play is over: its end posted (not to a mod going away), its slot free. The actor is not touched.
            void end(Play& rPlay, u32 why) {
                if (luma::modlog::facts()) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "anim.end.%llu", static_cast< unsigned long long >(rPlay.handle));
                    char json[48];
                    nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", whyName(why));
                    luma::modlog::fact(key, json);
                }
                if (why != LUMA_ANIM_MOD) {
                    post(rPlay, LUMA_ANIM_EVENT_END, why == LUMA_ANIM_ENDED ? LUMA_OK : LUMA_E_CANCELLED, why);
                }
                rPlay = Play();
                sLive--;
                while (sHigh > 0 && sPlays[sHigh - 1].handle == 0) {
                    sHigh--;
                }
            }

            // ---- The service ----

            s32 playOn(const LumaMod* pMod, const char* pWhere, LiveActor* pActor, LumaHandle model, const char* pClip,
                       const LumaAnimHow* pHow, LumaEventFn fn, void* pUser, LumaAnimPlay* pOut) {
                LumaAnimHow how = {};
                how.size = sizeof(how);
                if (pHow != nullptr) {
                    if (pHow->size < sizeof(LumaAnimHow)) {
                        char found[16];
                        char expected[16];
                        nebula_snprintf(found, sizeof(found), "%u", pHow->size);
                        nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaAnimHow)));
                        report(pMod, pWhere, 103u, "LumaAnimHow", found, expected);
                        return LUMA_E_ARGUMENT;
                    }
                    how = *pHow;
                }
                const f32 rate = how.rate == 0.0f ? 1.0f : how.rate;
                const char* pWrong = pClip == nullptr || pClip[0] == 0             ? "clip"
                                     : strlen(pClip) >= kClipMost                  ? "clip"
                                     : !(rate > 0.0f && rate <= LUMA_ANIM_MOST_RATE) ? "rate"
                                     : how.blend < LUMA_ANIM_BLEND_NONE            ? "blend"
                                     : how.loop > static_cast< u32 >(LUMA_ANIM_LOOP) ? "loop"
                                     : !(how.start >= 0.0f) || !std::isfinite(how.start) ? "start"
                                     : how.mark_count > LUMA_ANIM_MOST_MARKS       ? "marks"
                                     : how.mark_count != 0 && how.marks == nullptr ? "marks"
                                     : pOut == nullptr                             ? "out"
                                                                                   : nullptr;
                for (u32 i = 0; pWrong == nullptr && i < how.mark_count; i++) {
                    if (!std::isfinite(how.marks[i])) {
                        pWrong = "marks";
                    }
                }
                if (pWrong != nullptr) {
                    report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                // The kinds of the clip the model has (references 6: none, Content.MissingAnimation).
                u8 kinds = 0;
                u32 lead = kKinds;
                for (u32 kind = 0; kind < kKinds; kind++) {
                    if (exists(pActor, kind, pClip)) {
                        kinds = static_cast< u8 >(kinds | (1u << kind));
                        if (lead == kKinds) {
                            lead = kind;
                        }
                    }
                }
                if (kinds == 0) {
                    const char* const pModel = modelNameOf(pActor);
                    char column[300];
                    nebula_snprintf(column, sizeof(column), "%s/%s", pModel, pClip);
                    sayContent(pMod, cMissingAnimationCode, "Content.MissingAnimation", MR::getHashCode(column), "model", pModel,
                               "found", pClip);
                    return LUMA_E_NOT_FOUND;
                }
                if (how.start > static_cast< f32 >(ctrlOf(pActor, lead)->getEnd())) {
                    report(pMod, pWhere, 101u, "start", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                u32 open = 0;
                u32 slot = kMostOpen;
                for (u32 i = 0; i < kMostOpen; i++) {
                    if (sPlays[i].handle == 0) {
                        if (slot == kMostOpen) {
                            slot = i;
                        }
                        if (i >= sHigh) {
                            break;
                        }
                    } else if (sPlays[i].mod == pMod) {
                        open++;
                    }
                }
                if (open >= LUMA_ANIM_MOST_PLAYS || slot == kMostOpen) {
                    char text[16];
                    nebula_snprintf(text, sizeof(text), "%u", open >= LUMA_ANIM_MOST_PLAYS ? LUMA_ANIM_MOST_PLAYS : kMostOpen);
                    report(pMod, pWhere, 104u, text, nullptr, nullptr);
                    return LUMA_E_LIMIT;
                }
                // Earlier plays on the actor of a kind this one starts are replaced (any mod's).
                for (u32 i = 0; i < sHigh; i++) {
                    if (sPlays[i].handle != 0 && sPlays[i].actor == pActor && (sPlays[i].kinds & kinds) != 0) {
                        end(sPlays[i], LUMA_ANIM_REPLACED);
                    }
                }
                // The slot may be above sHigh now (the ends lowered it); it is free either way.
                bool loops = false;
                for (u32 kind = 0; kind < kKinds; kind++) {
                    if ((kinds & (1u << kind)) == 0) {
                        continue;
                    }
                    start(pActor, kind, pClip, how.blend);
                    FrameCtrl* const pCtrl = ctrlOf(pActor, kind);
                    if (how.loop == LUMA_ANIM_ONCE) {
                        pCtrl->setAttribute(FrameCtrl::EMode_NONE);
                    } else if (how.loop == LUMA_ANIM_LOOP) {
                        pCtrl->setAttribute(FrameCtrl::EMode_LOOP);
                    }
                    pCtrl->setRate(rate);
                    if (how.start > 0.0f) {
                        const f32 last = static_cast< f32 >(pCtrl->getEnd());
                        pCtrl->setFrame(how.start < last ? how.start : last);
                    }
                    if (kind == lead) {
                        const u8 attribute = pCtrl->getAttribute();
                        loops = attribute == FrameCtrl::EMode_LOOP || attribute == FrameCtrl::EMode_LOOP_REVERSE;
                    }
                }

                Play& rPlay = sPlays[slot];
                rPlay = Play();
                rPlay.handle = ++sNextHandle;
                rPlay.mod = pMod;
                rPlay.actor = pActor;
                rPlay.model = model;
                rPlay.kinds = kinds;
                rPlay.lead = static_cast< u8 >(lead);
                rPlay.loops = loops;
                rPlay.seen = ctrlOf(pActor, lead)->getFrame();
                rPlay.markCount = how.mark_count;
                for (u32 i = 0; i < how.mark_count; i++) {
                    rPlay.marks[i] = how.marks[i];
                }
                rPlay.events = findEvents(pMod, pActor, pClip, &rPlay.eventRows, &rPlay.eventFrame, &rPlay.eventName);
                if (rPlay.events == nullptr) {
                    rPlay.eventRows = 0;
                } else {
                    // A row past the lead's last frame never comes (the cook checks a game clip's, not a mod's own:
                    // animation 2a): Content.EventPastEnd, each start of such a play.
                    Table table;
                    table.attach(rPlay.events);
                    const s32 last = static_cast< s32 >(ctrlOf(pActor, lead)->getEnd());
                    for (u32 r = 0; r < rPlay.eventRows; r++) {
                        s32 at = 0;
                        const char* pName = nullptr;
                        if (!table.getValueFast(static_cast< int >(r), rPlay.eventFrame, &at) || at <= last ||
                            !table.getValueFast(static_cast< int >(r), rPlay.eventName, &pName)) {
                            continue;
                        }
                        char frame[16];
                        char lastText[16];
                        nebula_snprintf(frame, sizeof(frame), "%d", at);
                        nebula_snprintf(lastText, sizeof(lastText), "%d", last);
                        char column[300];
                        nebula_snprintf(column, sizeof(column), "%s/%s", pClip, pName);
                        LumaModLogEntry entry = {};
                        entry.size = sizeof(entry);
                        entry.source = LUMA_MODLOG_CONTENT;
                        entry.level = LUMA_MODLOG_ERROR;
                        entry.code = cEventPastEndCode;
                        entry.mod = sKernel->mod_id(pMod);
                        entry.file = "";
                        entry.column = MR::getHashCode(column);
                        const char* const names[4] = {"found", "clip", "frame", "last"};
                        const char* const values[4] = {pName, pClip, frame, lastText};
                        for (u32 i = 0; i < 4; i++) {
                            entry.names[i] = names[i];
                            entry.values[i] = values[i];
                        }
                        entry.count = 4;
                        (void)luma::modlog::add(entry, "Content.EventPastEnd");
                    }
                }
                rPlay.fn = fn;
                rPlay.user = pUser;
                memcpy(rPlay.clip, pClip, strlen(pClip) + 1);
                if (slot >= sHigh) {
                    sHigh = slot + 1;
                }
                sLive++;
                if (luma::modlog::facts()) {
                    char modelText[160];
                    char clip[300];
                    luma::modlog::jsonString(modelText, sizeof(modelText), modelNameOf(pActor));
                    luma::modlog::jsonString(clip, sizeof(clip), pClip);
                    char kindList[40];
                    u32 at = 0;
                    kindList[0] = 0;
                    for (u32 kind = 0; kind < kKinds; kind++) {
                        if ((kinds & (1u << kind)) != 0) {
                            at += static_cast< u32 >(nebula_snprintf(kindList + at, sizeof(kindList) - at, "%s\"%s\"",
                                                                     at == 0 ? "" : ",", cKindNames[kind]));
                        }
                    }
                    char json[768];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"model\":%s,\"clip\":%s,\"kinds\":[%s],\"loops\":%s,\"frames\":%d,\"start\":%.3f,"
                                    "\"rate\":%.3f,\"on_model\":%s,\"event_rows\":%u}",
                                    modelText, clip, kindList, loops ? "true" : "false",
                                    static_cast< int >(ctrlOf(pActor, lead)->getEnd()), static_cast< double >(rPlay.seen),
                                    static_cast< double >(rate), model != 0 ? "true" : "false", rPlay.eventRows);
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "anim.play.%llu", static_cast< unsigned long long >(rPlay.handle));
                    luma::modlog::fact(key, json);
                }
                // A mark or an event row on the frame it starts from is passed now.
                for (u32 i = 0; i < rPlay.markCount; i++) {
                    if (rPlay.marks[i] == rPlay.seen) {
                        mark(rPlay, i, rPlay.seen);
                    }
                }
                if (rPlay.events != nullptr) {
                    Table table;
                    table.attach(rPlay.events);
                    for (u32 r = 0; r < rPlay.eventRows; r++) {
                        s32 at = 0;
                        const char* pName = nullptr;
                        if (table.getValueFast(static_cast< int >(r), rPlay.eventFrame, &at) && static_cast< f32 >(at) == rPlay.seen &&
                            table.getValueFast(static_cast< int >(r), rPlay.eventName, &pName)) {
                            mark(rPlay, rPlay.markCount + r, rPlay.seen, pName);
                        }
                    }
                }
                *pOut = rPlay.handle;
                return LUMA_OK;
            }

            bool isLiveActor(LiveActor* pActor) {
                return pActor != nullptr && lumacore::isA(lumacore::classOf(pActor), lumacore::kClassId< LiveActor >) &&
                       !MR::isDead(pActor) && pActor->mModelManager != nullptr;
            }

            int32_t svcPlay(const LumaMod* pMod, LumaActor actor, const char* pClip, const LumaAnimHow* pHow, LumaEventFn fn,
                            void* pUser, LumaAnimPlay* pOut) {
                const char* const pWhere = "anim.play";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                LiveActor* const pActor = static_cast< LiveActor* >(actor);
                if (!isLiveActor(pActor)) {
                    report(pMod, pWhere, 101u, "actor", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                return playOn(pMod, pWhere, pActor, 0, pClip, pHow, fn, pUser, pOut);
            }

            int32_t svcPlayOnModel(const LumaMod* pMod, LumaContentModel model, const char* pClip, const LumaAnimHow* pHow,
                                   LumaEventFn fn, void* pUser, LumaAnimPlay* pOut) {
                const char* const pWhere = "anim.play_on_model";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                bool open = false;
                LiveActor* const pPart = luma::content::model::partOf(pMod, model, &open);
                if (!open) {
                    report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pPart == nullptr || !isLiveActor(pPart)) {
                    // Nothing shows (neither the model nor the placeholder is in a store): no clip of any name.
                    const char* const pFound = pClip != nullptr ? pClip : "";
                    sayContent(pMod, cMissingAnimationCode, "Content.MissingAnimation", MR::getHashCode(pFound), "model", "-",
                               "found", pFound);
                    return LUMA_E_NOT_FOUND;
                }
                return playOn(pMod, pWhere, pPart, model, pClip, pHow, fn, pUser, pOut);
            }

            int32_t svcStop(const LumaMod* pMod, LumaAnimPlay play) {
                const char* const pWhere = "anim.stop";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Play* const pPlay = playOf(pMod, play);
                if (pPlay == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                // Each kind stays on its frame (a dead actor's are no one's to stop: frame() ends the play "actor").
                if (!MR::isDead(pPlay->actor)) {
                    for (u32 kind = 0; kind < kKinds; kind++) {
                        if ((pPlay->kinds & (1u << kind)) != 0 && runs(pPlay->actor, kind, pPlay->clip)) {
                            ctrlOf(pPlay->actor, kind)->setRate(0.0f);
                        }
                    }
                }
                end(*pPlay, LUMA_ANIM_STOPPED);
                return LUMA_OK;
            }

            int32_t svcFrame(const LumaMod* pMod, LumaAnimPlay play, float* pFrame, float* pLast) {
                const char* const pWhere = "anim.frame";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Play* const pPlay = playOf(pMod, play);
                if (pPlay == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                const FrameCtrl* const pCtrl = ctrlOf(pPlay->actor, pPlay->lead);
                const f32 frame = pCtrl->getFrame();
                const f32 last = static_cast< f32 >(pCtrl->getEnd());
                if (pFrame != nullptr) {
                    *pFrame = frame;
                }
                if (pLast != nullptr) {
                    *pLast = last;
                }
                if (luma::modlog::facts()) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "anim.frame.%llu", static_cast< unsigned long long >(pPlay->handle));
                    char json[96];
                    nebula_snprintf(json, sizeof(json), "{\"frame\":%.3f,\"last\":%.3f}", static_cast< double >(frame),
                                    static_cast< double >(last));
                    luma::modlog::fact(key, json);
                }
                return LUMA_OK;
            }

            const LumaAnimService cTable = {
                {sizeof(LumaAnimService), LUMA_ANIM_MAJOR, LUMA_ANIM_MINOR},
                svcPlay,
                svcPlayOnModel,
                svcStop,
                svcFrame,
            };

            // ---- The provider ----

            // The mod's stop (or its taking out): its plays end; the clips go on as the actors' own.
            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                for (u32 i = 0; i < sHigh; i++) {
                    if (sPlays[i].handle != 0 && sPlays[i].mod == pMod) {
                        end(sPlays[i], LUMA_ANIM_MOD);
                    }
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_ANIM_NAME,
                &cTable.base,
                nullptr,
                nullptr,
                0,
                nullptr,
                nullptr,
                modGone,
                nullptr,
                registered,
            };

            // The marks the lead's frame passed from `from` to `to` (a loop's wrap: past its last frame and on from its
            // first; a clip going back: the marks it passed going back).
            bool passes(f32 at, f32 from, f32 to, bool wrapped) {
                return wrapped     ? at > from || at <= to
                       : to > from ? at > from && at <= to
                                   : at >= to && at < from;
            }

            void passMarks(const Play& rPlay, f32 from, f32 to, bool wrapped) {
                for (u32 i = 0; i < rPlay.markCount; i++) {
                    if (passes(rPlay.marks[i], from, to, wrapped)) {
                        mark(rPlay, i, to);
                    }
                }
                // The clip's own event rows, after them (their frames are whole).
                if (rPlay.events == nullptr) {
                    return;
                }
                Table table;
                table.attach(rPlay.events);
                for (u32 r = 0; r < rPlay.eventRows; r++) {
                    s32 at = 0;
                    const char* pName = nullptr;
                    if (table.getValueFast(static_cast< int >(r), rPlay.eventFrame, &at) &&
                        passes(static_cast< f32 >(at), from, to, wrapped) &&
                        table.getValueFast(static_cast< int >(r), rPlay.eventName, &pName)) {
                        mark(rPlay, rPlay.markCount + r, to, pName);
                    }
                }
            }
        }  // namespace

        void frame() {
            if (sLive == 0) {
                return;
            }
            for (u32 i = 0; i < sHigh; i++) {
                Play& rPlay = sPlays[i];
                if (rPlay.handle == 0) {
                    continue;
                }
                // A model's play: the model still attached and drawn by this part (a detached part is dead, and may be
                // used again for another handle).
                if (rPlay.model != 0) {
                    bool open = false;
                    if (luma::content::model::partOf(rPlay.mod, rPlay.model, &open) != rPlay.actor || !open) {
                        end(rPlay, LUMA_ANIM_ACTOR);
                        continue;
                    }
                }
                if (MR::isDead(rPlay.actor)) {
                    end(rPlay, LUMA_ANIM_ACTOR);
                    continue;
                }
                if (!runs(rPlay.actor, rPlay.lead, rPlay.clip)) {
                    end(rPlay, LUMA_ANIM_REPLACED);
                    continue;
                }
                const FrameCtrl* const pCtrl = ctrlOf(rPlay.actor, rPlay.lead);
                const f32 now = pCtrl->getFrame();
                if (pCtrl->getAttribute() == FrameCtrl::EMode_RESET && now < rPlay.seen) {
                    // Once, then back to its first frame: what it passed was the way to its last.
                    passMarks(rPlay, rPlay.seen, static_cast< f32 >(pCtrl->getEnd()), false);
                    rPlay.seen = now;
                } else if (now != rPlay.seen) {
                    const bool wrapped = pCtrl->getAttribute() == FrameCtrl::EMode_LOOP && now < rPlay.seen && pCtrl->getRate() > 0.0f;
                    passMarks(rPlay, rPlay.seen, now, wrapped);
                    rPlay.seen = now;
                }
                if (!rPlay.loops && ended(rPlay.actor, rPlay.lead)) {
                    end(rPlay, LUMA_ANIM_ENDED);
                }
            }
        }

        void released(const void* pStart, const void* pEnd) {
            if (sLive == 0) {
                return;
            }
            for (u32 i = 0; i < sHigh; i++) {
                Play& rPlay = sPlays[i];
                if (rPlay.handle != 0 && static_cast< const char* >(static_cast< const void* >(rPlay.actor)) >= static_cast< const char* >(pStart) &&
                    static_cast< const char* >(static_cast< const void* >(rPlay.actor)) < static_cast< const char* >(pEnd)) {
                    end(rPlay, LUMA_ANIM_SCENE);
                }
            }
        }

        const LumaServiceProvider* provider() { return &cProvider; }
    }
}
