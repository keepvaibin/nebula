#include "Game/System/LumaUi.hpp"

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/TextBox.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <luma_modlog.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <luma_ui.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <cstring>

// The ui service (plan 2.4 layer 4 on 3.8 layer 1; contract ui). Game thread throughout: the service's calls, the frame,
// the provider; NebulaArena::destroy is the scene end's, on the game thread as well.
//
// A screen is the game's own LayoutActor: made in the scene's arena (so it goes with the scene), connected as the game
// connects its HUD (or its pause menu's layouts), drawn by the game's layout drawer with the game's fonts and its 16:9
// widening. The scene's lists keep every actor they were given, so a closed screen's actor is dead and is used again
// for the same mod, layout and layer; the panes it found get back the visibility the layout gave them.
//
// No cost with no mod: frame() and released() are one load each while no screen was made.

namespace luma {
    namespace ui {
        namespace {
            const u32 kMostMade = LUMA_UI_MOST_SCREENS * 8u;  // screens made in one scene, across mods
            const u32 kMostOpen = LUMA_UI_MOST_SCREENS * 16u;
            const u32 kMostPanes = LUMA_UI_MOST_PANES * 4u;
            const u32 cMissingLayoutCode = 570;
            const u32 cMissingPaneCode = 571;
            const u32 cTextCutCode = 572;
            const u32 cNotTextCode = 573;
            const u32 cNoSceneCode = 574;
            const u32 cNotUtf8Code = 575;
            const u32 cMissingAnimationCode = 576;
            const u32 kMostEnds = kMostOpen * 2u;  // end events waiting or being given, across mods
            const u32 kMostBoxes = 32;              // text boxes one set_text reaches

            // A layout actor the service made: the game's LayoutActor, connected to the layer it was made for.
            class ScreenActor : public LayoutActor {
            public:
                ScreenActor(const char* pLayout, u32 layer) : LayoutActor("LumaCore screen", true) {
                    if (layer == LUMA_UI_PAUSE) {
                        MR::connectToSceneLayoutOnPause(this);
                    } else {
                        MR::connectToSceneLayout(this);
                    }
                    initLayoutManager(pLayout, 1);  // one animation layer on the root: ui.play
                }
            };

            struct Made {
                Made* next;           // every made screen, for reuse and release
                const LumaMod* mod;   // the mod it was made for (reused only for it)
                ScreenActor* actor;
                char* layout;         // the arena's copy
                u32 layer;
                bool busy;            // an open handle uses it
            };

            struct Screen {
                LumaHandle handle;  // 0: a free slot
                const LumaMod* mod;
                Made* made;
                u32 frame;          // frames the frame has seen it
                bool reused;
                // The animation play started, until its end is given: who hears it, and whether it loops (no end).
                bool playing;
                bool loop;
                LumaEventFn fn;
                void* user;
            };

            // A LUMA_UI_EVENT_ANIMATION_END on its way: its own copy of what it says (its screen may end or be used
            // again meanwhile). Busy from its post to the start of its deliver, or its drop.
            struct End {
                LumaEventNode node;
                bool busy;
                LumaHandle screen;
                s32 status;
                LumaEventFn fn;
                void* user;
            };

            struct Pane {
                LumaHandle handle;  // 0: a free slot
                LumaHandle screen;
                const LumaMod* mod;
                Ui::Pane* pane;
                bool visible;       // as the layout gave it, given back when the screen closes
            };

            const LumaServiceKernel* sKernel = nullptr;
            Screen sScreens[kMostOpen];
            u32 sHigh = 0;  // slots below are in use or were
            u32 sLive = 0;  // open handles
            Pane sPanes[kMostPanes];
            u32 sPaneHigh = 0;
            LumaHandle sNextHandle = 0;
            Made* sMade = nullptr;
            u32 sMadeCount = 0;
            End sEnds[kMostEnds];

            // ---- Calls ----

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1) {
                const char* args[2] = {pArg0, pArg1};
                u32 count = 0;
                while (count < 2 && args[count] != nullptr) {
                    count++;
                }
                sKernel->report(pMod, pWhere, code, args, count);
            }

            void say(const LumaMod* pMod, u32 code, const char* pRow, u32 column, const char* pName0, const char* pValue0,
                     const char* pName1, const char* pValue1) {
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
                    report(pMod, pWhere, 100u, nullptr, nullptr);
                    return LUMA_E_THREAD;
                }
                return LUMA_OK;
            }

            char* copyText(const char* pText) {
                const u32 length = static_cast< u32 >(strlen(pText));
                char* const pCopy = new char[length + 1];
                memcpy(pCopy, pText, length + 1);
                return pCopy;
            }

            // A scene that runs: its objects' arena and lists are there and it is past its start.
            bool sceneRunning() {
                const GameSystemSceneController* const pController = SingletonHolder< GameSystem >::get()->mSceneController;
                return Arenas::scene() != nullptr && pController != nullptr && pController->isExistSceneObjHolder() &&
                       pController->isNerveNormal();
            }

            Screen* screenOf(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < sHigh; i++) {
                    if (sScreens[i].handle == handle) {
                        return sScreens[i].mod == pMod ? &sScreens[i] : nullptr;
                    }
                }
                return nullptr;
            }

            Pane* paneOf(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < sPaneHigh; i++) {
                    if (sPanes[i].handle == handle) {
                        return sPanes[i].mod == pMod ? &sPanes[i] : nullptr;
                    }
                }
                return nullptr;
            }

            // The text boxes a set_text reaches, as the game's MR::executeTextBoxRecursive walks them: the pane itself
            // when it holds text, then every text box under it, in the tree's order. At most `most`; returns the count.
            u32 textBoxes(Ui::Pane* pPane, Ui::TextBox** ppOut, u32 count, u32 most) {
                if (count < most) {
                    if (Ui::TextBox* const pBox = Ui::PaneCast< Ui::TextBox >(pPane); pBox != nullptr) {
                        ppOut[count++] = pBox;
                    }
                }
                for (Ui::Pane* pChild : pPane->GetChildList()) {
                    if (count >= most) {
                        break;
                    }
                    count = textBoxes(pChild, ppOut, count, most);
                }
                return count;
            }

            // ---- Facts (harness only) ----

            void openFact(const Screen& rScreen) {
                char mod[160];
                char layout[300];
                luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rScreen.mod));
                luma::modlog::jsonString(layout, sizeof(layout), rScreen.made->layout);
                char json[640];
                nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"layout\":%s,\"layer\":\"%s\",\"reused\":%s}", mod, layout,
                                rScreen.made->layer == LUMA_UI_PAUSE ? "pause" : "hud", rScreen.reused ? "true" : "false");
                char key[48];
                nebula_snprintf(key, sizeof(key), "ui.open.%llu", static_cast< unsigned long long >(rScreen.handle));
                luma::modlog::fact(key, json);
            }

            void paneFact(const Pane& rPane, const char* pWhat) {
                char name[160];
                luma::modlog::jsonString(name, sizeof(name), rPane.pane->mName);
                char text[1200];
                text[0] = 0;
                Ui::TextBox* boxes[kMostBoxes];
                const u32 boxCount = textBoxes(rPane.pane, boxes, 0, kMostBoxes);
                if (const Ui::TextBox* const pText = boxCount != 0 ? boxes[0] : nullptr; pText != nullptr) {
                    // The text as the pane holds it, back to UTF-8 (the harness compares it with what the mod gave).
                    char utf8[1024];
                    u32 at = 0;
                    const char16_t* const pString = pText->mTextBuf;
                    const u32 length = pString != nullptr ? pText->mTextLen : 0u;
                    for (u32 i = 0; i < length && pString[i] != 0 && at + 5 < sizeof(utf8); i++) {
                        u32 point = pString[i];
                        if (point >= 0xD800 && point <= 0xDBFF && i + 1 < length && pString[i + 1] >= 0xDC00 && pString[i + 1] <= 0xDFFF) {
                            point = 0x10000u + ((point - 0xD800u) << 10u) + (pString[i + 1] - 0xDC00u);
                            i++;
                        }
                        if (point < 0x80u) {
                            utf8[at++] = static_cast< char >(point);
                        } else if (point < 0x800u) {
                            utf8[at++] = static_cast< char >(0xC0u | (point >> 6u));
                            utf8[at++] = static_cast< char >(0x80u | (point & 0x3Fu));
                        } else if (point < 0x10000u) {
                            utf8[at++] = static_cast< char >(0xE0u | (point >> 12u));
                            utf8[at++] = static_cast< char >(0x80u | ((point >> 6u) & 0x3Fu));
                            utf8[at++] = static_cast< char >(0x80u | (point & 0x3Fu));
                        } else {
                            utf8[at++] = static_cast< char >(0xF0u | (point >> 18u));
                            utf8[at++] = static_cast< char >(0x80u | ((point >> 12u) & 0x3Fu));
                            utf8[at++] = static_cast< char >(0x80u | ((point >> 6u) & 0x3Fu));
                            utf8[at++] = static_cast< char >(0x80u | (point & 0x3Fu));
                        }
                    }
                    utf8[at] = 0;
                    char quoted[1100];
                    luma::modlog::jsonString(quoted, sizeof(quoted), utf8);
                    nebula_snprintf(text, sizeof(text), ",\"text\":%s,\"boxes\":%u", quoted, boxCount);
                }
                char json[1500];
                nebula_snprintf(json, sizeof(json), "{\"screen\":%llu,\"pane\":%s,\"kind\":%u,\"visible\":%s%s}",
                                static_cast< unsigned long long >(rPane.screen), name, static_cast< u32 >(rPane.pane->mKind),
                                rPane.pane->IsVisible() ? "true" : "false", text);
                char key[48];
                nebula_snprintf(key, sizeof(key), "ui.%s.%llu", pWhat, static_cast< unsigned long long >(rPane.handle));
                luma::modlog::fact(key, json);
            }

            // ---- Animation ends ----

            void deliverEnd(LumaEventNode* pNode) {
                End* const pEnd = reinterpret_cast< End* >(pNode);
                const LumaMod* const pMod = pNode->mod;
                LumaEvent event = {};
                event.size = sizeof(event);
                event.kind = LUMA_UI_EVENT_ANIMATION_END;
                event.status = pEnd->status;
                event.handle = pEnd->screen;
                const LumaEventFn fn = pEnd->fn;
                void* const pUser = pEnd->user;
                pEnd->busy = false;  // free again before the mod's function, which may play again
                (void)sKernel->call(pMod, LUMA_UI_NAME, fn, pUser, &event);
            }

            void dropEnd(LumaEventNode* pNode) {
                reinterpret_cast< End* >(pNode)->busy = false;
            }

            // The screen's animation is over (`status`): its end posted when the mod asked for one.
            void finishPlay(Screen& rScreen, s32 status) {
                if (!rScreen.playing) {
                    return;
                }
                rScreen.playing = false;
                if (rScreen.fn == nullptr) {
                    return;
                }
                for (u32 i = 0; i < kMostEnds; i++) {
                    End& rEnd = sEnds[i];
                    if (rEnd.busy) {
                        continue;
                    }
                    rEnd.busy = true;
                    rEnd.node.mod = rScreen.mod;
                    rEnd.node.deliver = deliverEnd;
                    rEnd.node.drop = dropEnd;
                    rEnd.screen = rScreen.handle;
                    rEnd.status = status;
                    rEnd.fn = rScreen.fn;
                    rEnd.user = rScreen.user;
                    if (sKernel->post(&rEnd.node) != LUMA_OK) {
                        rEnd.busy = false;  // the mod is gone: no one to tell
                    }
                    return;
                }
                nebula_log("ui: no room for an animation end of %s; it is not given\n", sKernel->mod_id(rScreen.mod));
            }

            // ---- Ends ----

            void endPanes(LumaHandle screen, bool restore) {
                for (u32 i = 0; i < sPaneHigh; i++) {
                    Pane& rPane = sPanes[i];
                    if (rPane.handle == 0 || rPane.screen != screen) {
                        continue;
                    }
                    if (restore) {
                        rPane.pane->SetVisible(rPane.visible);
                    }
                    rPane = Pane();
                }
                while (sPaneHigh > 0 && sPanes[sPaneHigh - 1].handle == 0) {
                    sPaneHigh--;
                }
            }

            // `stop`: the actor is still the scene's (close, the mod's end); else its memory is going (the scene's end).
            void end(Screen& rScreen, const char* pWhy, bool stop) {
                if (luma::modlog::facts()) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "ui.end.%llu", static_cast< unsigned long long >(rScreen.handle));
                    char json[48];
                    nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                    luma::modlog::fact(key, json);
                }
                finishPlay(rScreen, LUMA_E_CANCELLED);
                endPanes(rScreen.handle, stop);
                if (Made* const pMade = rScreen.made; pMade != nullptr && stop) {
                    MR::stopAnim(pMade->actor, 0);
                    if (!MR::isDead(pMade->actor)) {
                        pMade->actor->kill();
                    }
                    MR::showLayout(pMade->actor);
                    pMade->busy = false;
                }
                rScreen = Screen();
                sLive--;
                while (sHigh > 0 && sScreens[sHigh - 1].handle == 0) {
                    sHigh--;
                }
            }

            // UTF-8 to the pane's UTF-16, at most `most` units (a pair is never split). False: not UTF-8. *pCut: cut.
            bool toUtf16(const char* pText, char16_t* pOut, u32 most, bool* pCut) {
                u32 at = 0;
                *pCut = false;
                for (u32 i = 0; pText[i] != 0;) {
                    const u8 b = static_cast< u8 >(pText[i]);
                    u32 more = 0;
                    u32 point = b;
                    if (b >= 0xF0u && b <= 0xF4u) {
                        more = 3;
                        point = b & 0x07u;
                    } else if (b >= 0xE0u && b <= 0xEFu) {
                        more = 2;
                        point = b & 0x0Fu;
                    } else if (b >= 0xC2u && b <= 0xDFu) {
                        more = 1;
                        point = b & 0x1Fu;
                    } else if (b >= 0x80u) {
                        return false;
                    }
                    for (u32 k = 1; k <= more; k++) {
                        const u8 c = static_cast< u8 >(pText[i + k]);
                        if ((c & 0xC0u) != 0x80u) {
                            return false;  // a 0 ends it here as well: cut short
                        }
                        point = (point << 6u) | (c & 0x3Fu);
                    }
                    static const u32 cLeast[4] = {0u, 0x80u, 0x800u, 0x10000u};
                    if (point < cLeast[more] || (point >= 0xD800u && point <= 0xDFFFu) || point > 0x10FFFFu) {
                        return false;
                    }
                    i += more + 1;
                    const u32 units = point >= 0x10000u ? 2u : 1u;
                    if (*pCut || at + units > most) {
                        *pCut = true;
                        continue;  // the rest is still checked
                    }
                    if (units == 2u) {
                        pOut[at++] = static_cast< char16_t >(0xD800u + ((point - 0x10000u) >> 10u));
                        pOut[at++] = static_cast< char16_t >(0xDC00u + ((point - 0x10000u) & 0x3FFu));
                    } else {
                        pOut[at++] = static_cast< char16_t >(point);
                    }
                }
                pOut[at] = 0;
                return true;
            }

            // ---- The service ----

            int32_t svcOpen(const LumaMod* pMod, const char* pLayout, LumaUiLayer layer, LumaUiScreen* pOut) {
                const char* const pWhere = "ui.open";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                const u32 placed = static_cast< u32 >(layer);
                const char* const pWrong = pLayout == nullptr || pLayout[0] == 0 ? "layout"
                                           : placed > LUMA_UI_PAUSE              ? "layer"
                                           : pOut == nullptr                     ? "out"
                                                                                 : nullptr;
                if (pWrong != nullptr) {
                    report(pMod, pWhere, 101u, pWrong, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (!sceneRunning()) {
                    report(pMod, pWhere, cNoSceneCode, nullptr, nullptr);
                    return LUMA_E_BUSY;
                }
                u32 open = 0;
                u32 slot = kMostOpen;
                for (u32 i = 0; i < kMostOpen; i++) {
                    if (sScreens[i].handle == 0) {
                        if (slot == kMostOpen) {
                            slot = i;
                        }
                        if (i >= sHigh) {
                            break;
                        }
                    } else if (sScreens[i].mod == pMod) {
                        open++;
                    }
                }
                if (open >= LUMA_UI_MOST_SCREENS || slot == kMostOpen) {
                    char text[16];
                    nebula_snprintf(text, sizeof(text), "%u", open >= LUMA_UI_MOST_SCREENS ? LUMA_UI_MOST_SCREENS : kMostOpen);
                    report(pMod, pWhere, 104u, text, nullptr);
                    return LUMA_E_LIMIT;
                }
                // The layout: the mod's cooked one or the game's, LayoutData/<layout>.arc in a store (references 4: a
                // screen has no placeholder; nothing is drawn).
                MR::FormattedName path;
                if (!MR::makeLayoutArchiveFileNameFromPrefix(path, pLayout, false)) {
                    say(pMod, cMissingLayoutCode, "Ui.MissingLayout", MR::getHashCode(pLayout), "found", pLayout, nullptr, nullptr);
                    return LUMA_E_NOT_FOUND;
                }
                // A dead one of this mod's for this layout and layer, else a new one in the scene's arena.
                Made* pMade = nullptr;
                for (Made* p = sMade; p != nullptr; p = p->next) {
                    if (!p->busy && p->mod == pMod && p->layer == placed && strcmp(p->layout, pLayout) == 0) {
                        pMade = p;
                        break;
                    }
                }
                const bool reused = pMade != nullptr;
                if (pMade == nullptr) {
                    if (sMadeCount >= kMostMade) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", kMostMade);
                        report(pMod, pWhere, 104u, text, nullptr);
                        return LUMA_E_LIMIT;
                    }
                    const NebulaArena::CurrentScope scope(Arenas::scene());
                    pMade = new Made();
                    pMade->mod = pMod;
                    pMade->layout = copyText(pLayout);
                    pMade->layer = placed;
                    pMade->actor = new ScreenActor(pMade->layout, placed);
                    pMade->next = sMade;
                    sMade = pMade;
                    sMadeCount++;
                }
                pMade->busy = true;
                MR::showLayout(pMade->actor);
                pMade->actor->appear();

                Screen& rScreen = sScreens[slot];
                rScreen = Screen();
                rScreen.handle = ++sNextHandle;
                rScreen.mod = pMod;
                rScreen.made = pMade;
                rScreen.reused = reused;
                if (slot >= sHigh) {
                    sHigh = slot + 1;
                }
                sLive++;
                if (luma::modlog::facts()) {
                    openFact(rScreen);
                }
                *pOut = rScreen.handle;
                return LUMA_OK;
            }

            int32_t svcPane(const LumaMod* pMod, LumaUiScreen screen, const char* pName, LumaUiPane* pOut) {
                const char* const pWhere = "ui.pane";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pName == nullptr || pName[0] == 0 || pOut == nullptr) {
                    report(pMod, pWhere, 101u, pOut == nullptr ? "out" : "name", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Ui::Pane* const pFound = pScreen->made->actor->getLayoutManager()->findPaneOrRoot(pName);
                if (pFound == nullptr || strcmp(pFound->mName, pName) != 0) {
                    char column[300];
                    nebula_snprintf(column, sizeof(column), "%s/%s", pScreen->made->layout, pName);
                    say(pMod, cMissingPaneCode, "Ui.MissingPane", MR::getHashCode(column), "layout", pScreen->made->layout, "found",
                        pName);
                    return LUMA_E_NOT_FOUND;
                }
                u32 slot = kMostPanes;
                u32 found = 0;
                for (u32 i = 0; i < kMostPanes; i++) {
                    if (sPanes[i].handle == 0) {
                        if (slot == kMostPanes) {
                            slot = i;
                        }
                        if (i >= sPaneHigh) {
                            break;
                        }
                        continue;
                    }
                    if (sPanes[i].screen == screen && sPanes[i].pane == pFound) {
                        *pOut = sPanes[i].handle;
                        return LUMA_OK;
                    }
                    if (sPanes[i].mod == pMod) {
                        found++;
                    }
                }
                if (found >= LUMA_UI_MOST_PANES || slot == kMostPanes) {
                    char text[16];
                    nebula_snprintf(text, sizeof(text), "%u", found >= LUMA_UI_MOST_PANES ? LUMA_UI_MOST_PANES : kMostPanes);
                    report(pMod, pWhere, 104u, text, nullptr);
                    return LUMA_E_LIMIT;
                }
                Pane& rPane = sPanes[slot];
                rPane.handle = ++sNextHandle;
                rPane.screen = screen;
                rPane.mod = pMod;
                rPane.pane = pFound;
                rPane.visible = pFound->IsVisible();
                if (slot >= sPaneHigh) {
                    sPaneHigh = slot + 1;
                }
                if (luma::modlog::facts()) {
                    paneFact(rPane, "pane");
                }
                *pOut = rPane.handle;
                return LUMA_OK;
            }

            int32_t svcSetText(const LumaMod* pMod, LumaUiPane pane, const char* pText) {
                const char* const pWhere = "ui.set_text";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Pane* const pPane = paneOf(pMod, pane);
                if (pPane == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                Ui::TextBox* boxes[kMostBoxes];
                const u32 boxCount = textBoxes(pPane->pane, boxes, 0, kMostBoxes);
                if (boxCount == 0) {
                    report(pMod, pWhere, cNotTextCode, pPane->pane->mName, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (pText == nullptr) {
                    report(pMod, pWhere, 101u, "text", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                // A box's buffer holds its 0 as well; 1.0 promises LUMA_UI_TEXT_MOST units. Every box gets the same
                // text, so the smallest buffer cuts it.
                u32 most = LUMA_UI_TEXT_MOST;
                for (u32 i = 0; i < boxCount; i++) {
                    const u32 buffer = boxes[i]->GetStringBufferLength();
                    const u32 holds = buffer == 0 ? 0u : buffer - 1u;
                    if (holds < most) {
                        most = holds;
                    }
                }
                char16_t text[LUMA_UI_TEXT_MOST + 1];
                bool cut = false;
                if (!toUtf16(pText, text, most, &cut)) {
                    report(pMod, pWhere, cNotUtf8Code, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (cut) {
                    char limit[16];
                    nebula_snprintf(limit, sizeof(limit), "%u", most);
                    say(pMod, cTextCutCode, "Ui.TextCut", MR::getHashCode(pPane->pane->mName), "pane", pPane->pane->mName, "count",
                        limit);
                }
                for (u32 i = 0; i < boxCount; i++) {
                    boxes[i]->SetString(text, 0);
                }
                if (luma::modlog::facts()) {
                    paneFact(*pPane, "text");
                }
                return LUMA_OK;
            }

            int32_t svcSetVisible(const LumaMod* pMod, LumaUiPane pane, bool on) {
                const char* const pWhere = "ui.set_visible";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Pane* const pPane = paneOf(pMod, pane);
                if (pPane == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                pPane->pane->SetVisible(on);
                if (luma::modlog::facts()) {
                    paneFact(*pPane, "visible");
                }
                return LUMA_OK;
            }

            int32_t svcShow(const LumaMod* pMod, LumaUiScreen screen, bool on) {
                const char* const pWhere = "ui.show";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (on) {
                    MR::showLayout(pScreen->made->actor);
                } else {
                    MR::hideLayout(pScreen->made->actor);
                }
                return LUMA_OK;
            }

            int32_t svcClose(const LumaMod* pMod, LumaUiScreen screen) {
                const char* const pWhere = "ui.close";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                end(*pScreen, "close", true);
                return LUMA_OK;
            }

            int32_t svcPlay(const LumaMod* pMod, LumaUiScreen screen, const char* pAnimation, bool loop, LumaEventFn fn,
                            void* pUser) {
                const char* const pWhere = "ui.play";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pAnimation == nullptr || pAnimation[0] == 0) {
                    report(pMod, pWhere, 101u, "animation", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                ScreenActor* const pActor = pScreen->made->actor;
                if (pActor->getLayoutManager()->getAnimTransform(pAnimation) == nullptr) {
                    char column[300];
                    nebula_snprintf(column, sizeof(column), "%s/%s", pScreen->made->layout, pAnimation);
                    say(pMod, cMissingAnimationCode, "Ui.MissingAnimation", MR::getHashCode(column), "layout",
                        pScreen->made->layout, "found", pAnimation);
                    return LUMA_E_NOT_FOUND;
                }
                finishPlay(*pScreen, LUMA_E_CANCELLED);
                MR::startAnim(pActor, pAnimation, 0);
                // Once or looping as the mod asks, whatever the data says.
                MR::getAnimCtrl(pActor, 0)->setAttribute(loop ? FrameCtrl::EMode_LOOP : FrameCtrl::EMode_NONE);
                pScreen->playing = true;
                pScreen->loop = loop;
                pScreen->fn = fn;
                pScreen->user = pUser;
                if (luma::modlog::facts()) {
                    char name[160];
                    luma::modlog::jsonString(name, sizeof(name), pAnimation);
                    char json[256];
                    nebula_snprintf(json, sizeof(json), "{\"animation\":%s,\"loop\":%s,\"frames\":%d}", name,
                                    loop ? "true" : "false", static_cast< int >(MR::getAnimFrameMax(pActor, 0u)));
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "ui.play.%llu", static_cast< unsigned long long >(pScreen->handle));
                    luma::modlog::fact(key, json);
                }
                return LUMA_OK;
            }

            int32_t svcStop(const LumaMod* pMod, LumaUiScreen screen) {
                const char* const pWhere = "ui.stop";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pScreen->playing) {
                    MR::stopAnim(pScreen->made->actor, 0);
                    finishPlay(*pScreen, LUMA_E_CANCELLED);
                }
                return LUMA_OK;
            }

            const LumaUiService cTable = {
                {sizeof(LumaUiService), LUMA_UI_MAJOR, LUMA_UI_MINOR},
                svcOpen,
                svcPane,
                svcSetText,
                svcSetVisible,
                svcShow,
                svcClose,
                svcPlay,
                svcStop,
            };

            // The service's rows (services-registry 11: ui's block, 570-619).
            const LumaServiceRow cRows[] = {
                {cNotTextCode, "Ui.NotText", {"where", "pane", nullptr, nullptr}},
                {cNoSceneCode, "Ui.NoScene", {"where", nullptr, nullptr, nullptr}},
                {cNotUtf8Code, "Ui.NotUtf8", {"where", nullptr, nullptr, nullptr}},
            };

            // ---- The provider ----

            // The mod's stop (or its taking out): its screens stop drawing.
            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                for (u32 i = 0; i < sHigh; i++) {
                    if (sScreens[i].handle != 0 && sScreens[i].mod == pMod) {
                        end(sScreens[i], "mod", true);
                    }
                }
                // Its dead screens are no one's: another mod never reuses them, and the scene's end frees them.
                for (Made* p = sMade; p != nullptr; p = p->next) {
                    if (p->mod == pMod) {
                        p->mod = nullptr;
                    }
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_UI_NAME,
                &cTable.base,
                nullptr,
                cRows,
                sizeof(cRows) / sizeof(cRows[0]),
                nullptr,
                nullptr,
                modGone,
                nullptr,
                registered,
            };
        }  // namespace

        void frame() {
            if (sLive == 0) {
                return;
            }
            // Animations played to their end (a loop never ends).
            for (u32 i = 0; i < sHigh; i++) {
                Screen& rScreen = sScreens[i];
                if (rScreen.handle != 0 && rScreen.playing && !rScreen.loop && MR::isAnimStopped(rScreen.made->actor, 0)) {
                    finishPlay(rScreen, LUMA_OK);
                }
            }
            if (!luma::modlog::facts()) {
                return;
            }
            for (u32 i = 0; i < sHigh; i++) {
                Screen& rScreen = sScreens[i];
                if (rScreen.handle == 0 || rScreen.frame >= 120u) {
                    continue;
                }
                // Whether the game draws it this frame (its layer runs, it is shown and alive): the picture's companion.
                const LayoutActor* const pActor = rScreen.made->actor;
                char json[96];
                nebula_snprintf(json, sizeof(json), "{\"alive\":%s,\"hidden\":%s,\"drawn\":%s}", MR::isDead(pActor) ? "false" : "true",
                                MR::isHiddenLayout(pActor) ? "true" : "false", MR::isExecuteDrawLayout(pActor) ? "true" : "false");
                char key[48];
                nebula_snprintf(key, sizeof(key), "ui.screen.%llu", static_cast< unsigned long long >(rScreen.handle));
                luma::modlog::fact(key, json);
                rScreen.frame++;
            }
        }

        void released(const void* pStart, const void* pEnd) {
            if (sMade == nullptr) {
                return;
            }
            const auto inRange = [pStart, pEnd](const void* p) {
                return static_cast< const char* >(p) >= static_cast< const char* >(pStart) &&
                       static_cast< const char* >(p) < static_cast< const char* >(pEnd);
            };
            for (u32 i = 0; i < sHigh; i++) {
                Screen& rScreen = sScreens[i];
                if (rScreen.handle != 0 && inRange(rScreen.made)) {
                    end(rScreen, "scene", false);
                }
            }
            Made** ppLink = &sMade;
            while (*ppLink != nullptr) {
                Made* const pMade = *ppLink;
                if (inRange(pMade)) {
                    *ppLink = pMade->next;
                    sMadeCount--;
                } else {
                    ppLink = &pMade->next;
                }
            }
        }

        const LumaServiceProvider* provider() { return &cProvider; }
    }
}
