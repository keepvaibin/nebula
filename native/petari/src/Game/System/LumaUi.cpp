#include "Game/System/LumaUi.hpp"

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/InputButtons.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Ui/Font.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/TextBox.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
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
            const u32 cNoGlyphCode = 577;
            const u32 kMostEnds = kMostOpen * 2u;  // end events waiting or being given, across mods
            const u32 kMostBoxes = 32;              // text boxes one set_text reaches

            // A layout actor the service made: the game's LayoutActor, connected to the layer it was made for.
            class ScreenActor : public LayoutActor {
            public:
                ScreenActor(const char* pLayout, u32 layer, bool modLayout) : LayoutActor("LumaCore screen", true) {
                    if (layer == LUMA_UI_PAUSE) {
                        MR::connectToSceneLayoutOnPause(this);
                    } else {
                        MR::connectToSceneLayout(this);
                    }
                    // One animation layer on the root (ui.play). A mod's own layout keeps its text boxes as its cook made
                    // them (contract layout: `text`, and `length` where set_text cuts); the game's are set up as the game
                    // sets them (its code sets every box).
                    if (modLayout) {
                        initLayoutManagerKeepText(pLayout, 1);
                    } else {
                        initLayoutManager(pLayout, 1);
                    }
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

            // A screen's menu (ui 1.2): its rows' panes, which are on, the look each had, the focus, the row taken at the
            // last reading (-1 none).
            struct Menu {
                u32 count;
                Ui::Pane* panes[LUMA_UI_MENU_ROWS_MOST];
                bool on[LUMA_UI_MENU_ROWS_MOST];
                Vec2f scale[LUMA_UI_MENU_ROWS_MOST];
                u8 alpha[LUMA_UI_MENU_ROWS_MOST];
                s32 focus;
                s32 chosen;
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
                Menu menu;
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

            // The first code point of pText that rFont has no glyph for, 0 when it draws them all. A control code is not
            // drawn; a no-break space draws as the font's space (ResFontBase::GetGlyphIndex); past U+FFFF no font has one.
            u32 firstMissing(const Ui::Font& rFont, const char16_t* pText) {
                for (u32 i = 0; pText[i] != 0; i++) {
                    const u32 unit = pText[i];
                    if (unit >= 0xD800u && unit <= 0xDBFFu) {
                        const u32 low = pText[i + 1];
                        return 0x10000u + ((unit - 0xD800u) << 10u) + (low - 0xDC00u);
                    }
                    if (unit < 0x20u || rFont.HasGlyph(static_cast< Ui::CharCode >(unit))) {
                        continue;
                    }
                    if ((unit == 0xA0u || unit == 0x202Fu) && rFont.HasGlyph(0x20)) {
                        continue;
                    }
                    return unit;
                }
                return 0;
            }

            // Ui.NoGlyph {pane, found, code}: the font's stand-in is drawn in its place. One entry a pane and character.
            void sayNoGlyph(const LumaMod* pMod, const char* pPane, u32 point) {
                char found[5] = {};
                if (point < 0x80u) {
                    found[0] = static_cast< char >(point);
                } else if (point < 0x800u) {
                    found[0] = static_cast< char >(0xC0u | (point >> 6u));
                    found[1] = static_cast< char >(0x80u | (point & 0x3Fu));
                } else if (point < 0x10000u) {
                    found[0] = static_cast< char >(0xE0u | (point >> 12u));
                    found[1] = static_cast< char >(0x80u | ((point >> 6u) & 0x3Fu));
                    found[2] = static_cast< char >(0x80u | (point & 0x3Fu));
                } else {
                    found[0] = static_cast< char >(0xF0u | (point >> 18u));
                    found[1] = static_cast< char >(0x80u | ((point >> 12u) & 0x3Fu));
                    found[2] = static_cast< char >(0x80u | ((point >> 6u) & 0x3Fu));
                    found[3] = static_cast< char >(0x80u | (point & 0x3Fu));
                }
                char code[12];
                nebula_snprintf(code, sizeof(code), "U+%04X", point);
                char column[96];
                nebula_snprintf(column, sizeof(column), "%s/%s", pPane, code);
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CONTENT;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = cNoGlyphCode;
                entry.mod = sKernel->mod_id(pMod);
                entry.file = "";
                entry.column = MR::getHashCode(column);
                entry.names[0] = "pane";
                entry.values[0] = pPane;
                entry.names[1] = "found";
                entry.values[1] = found;
                entry.names[2] = "code";
                entry.values[2] = code;
                entry.count = 3;
                (void)luma::modlog::add(entry, "Ui.NoGlyph");
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

            // A scene that runs: its objects' arena and lists are there, it is past its start, and the game's fonts are
            // the system archive's. Before GameSystem::initAfterSystemArchiveLoaded the message font is the one built into
            // the program (ErrorMessageArchive's MessageFont26, without digits), and making the file fonts deletes it: a
            // layout made then keeps that font and its next draw goes through freed memory.
            bool sceneRunning() {
                const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
                const GameSystemSceneController* const pController = pSystem->mSceneController;
                const GameSystemFontHolder* const pFonts = pSystem->mFontHolder;
                return Arenas::scene() != nullptr && pController != nullptr && pController->isExistSceneObjHolder() &&
                       pController->isNerveNormal() && pFonts != nullptr && pFonts->mMessageFont != nullptr &&
                       pFonts->mEmbeddedMessageFont == nullptr;
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
            // A null `ppOut` counts them only.
            u32 textBoxes(Ui::Pane* pPane, Ui::TextBox** ppOut, u32 count, u32 most) {
                if (count < most) {
                    if (Ui::TextBox* const pBox = Ui::PaneCast< Ui::TextBox >(pPane); pBox != nullptr) {
                        if (ppOut != nullptr) {
                            ppOut[count] = pBox;
                        }
                        count++;
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
                Ui::TextBox* pFirst = nullptr;
                const u32 boxCount = textBoxes(rPane.pane, nullptr, 0, kMostBoxes);
                textBoxes(rPane.pane, &pFirst, 0, 1);
                if (const Ui::TextBox* const pText = pFirst; pText != nullptr) {
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
                    // ,"text":<quoted>,"boxes":N in one buffer: the quoted text leaves room for the count after it.
                    static const char kHead[] = ",\"text\":";
                    std::memcpy(text, kHead, sizeof(kHead));
                    const u32 head = sizeof(kHead) - 1u;
                    luma::modlog::jsonString(text + head, static_cast< u32 >(sizeof(text)) - head - 24u, utf8);
                    const u32 used = static_cast< u32 >(std::strlen(text));
                    nebula_snprintf(text + used, sizeof(text) - used, ",\"boxes\":%u", boxCount);
                }
                char json[1700];
                // The scale in thousandths (set_scale's; 1000 as the layout has it).
                const s32 scaleX = static_cast< s32 >(rPane.pane->mScale.x * 1000.0f + (rPane.pane->mScale.x < 0.0f ? -0.5f : 0.5f));
                const s32 scaleY = static_cast< s32 >(rPane.pane->mScale.y * 1000.0f + (rPane.pane->mScale.y < 0.0f ? -0.5f : 0.5f));
                // The place in layout units from the parent's origin (set_position's; the layout's own until set).
                nebula_snprintf(json, sizeof(json),
                                "{\"screen\":%llu,\"pane\":%s,\"kind\":%u,\"visible\":%s,\"scale_x\":%d,\"scale_y\":%d,\"x\":%.3f,"
                                "\"y\":%.3f%s}",
                                static_cast< unsigned long long >(rPane.screen), name, static_cast< u32 >(rPane.pane->mKind),
                                rPane.pane->IsVisible() ? "true" : "false", scaleX, scaleY,
                                static_cast< double >(rPane.pane->mTranslate.x), static_cast< double >(rPane.pane->mTranslate.y),
                                text);
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
            // The menu's rows given back their own look; the menu gone.
            void clearMenu(Menu& rMenu) {
                for (u32 i = 0; i < rMenu.count; i++) {
                    rMenu.panes[i]->mScale = rMenu.scale[i];
                    rMenu.panes[i]->mAlpha = rMenu.alpha[i];
                }
                rMenu.count = 0;
                rMenu.focus = -1;
                rMenu.chosen = -1;
            }

            // The rows drawn as the focus and on/off say.
            void lookMenu(const Menu& rMenu) {
                for (u32 i = 0; i < rMenu.count; i++) {
                    const f32 grow = static_cast< s32 >(i) == rMenu.focus ? 1.1f : 1.0f;
                    rMenu.panes[i]->mScale = Vec2f(rMenu.scale[i].x * grow, rMenu.scale[i].y * grow);
                    rMenu.panes[i]->mAlpha = rMenu.on[i] ? rMenu.alpha[i] : static_cast< u8 >(rMenu.alpha[i] / 2u);
                }
            }

            // The next row that is on from `from` stepping `step` (wrapping); -1 when none is.
            s32 nextOn(const Menu& rMenu, s32 from, s32 step) {
                const s32 count = static_cast< s32 >(rMenu.count);
                for (s32 k = 1; k <= count; k++) {
                    const s32 i = ((from + step * k) % count + count) % count;
                    if (rMenu.on[i]) {
                        return i;
                    }
                }
                return -1;
            }

            void end(Screen& rScreen, const char* pWhy, bool stop) {
                clearMenu(rScreen.menu);
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
                    // No game file at the path: a mod's own layout (one replacing the game's is the game's).
                    pMade->actor = new ScreenActor(pMade->layout, placed, GameAssets::findGame(path.c_str()) < 0);
                    // Made after the scene's connecting, its connects are requests that run at the next movement
                    // run's start, which a stopped scene (the pause) never reaches: they run now, on either layer.
                    MR::connectNow(pMade->actor);
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
                // 1.0 promises LUMA_UI_TEXT_MOST units. Every box gets the same text, so the smallest buffer cuts it
                // (GetStringBufferLength: the units a box holds, its 0 aside: a layout's `length`).
                u32 most = LUMA_UI_TEXT_MOST;
                for (u32 i = 0; i < boxCount; i++) {
                    const u32 holds = boxes[i]->GetStringBufferLength();
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
                // A character a box's font has no glyph for draws as the font's stand-in, often nothing (NumberFont has
                // no letters): the first such one is said for each font the boxes use.
                for (u32 i = 0; i < boxCount; i++) {
                    const Ui::Font* const pFont = boxes[i]->mpFont;
                    bool seen = pFont == nullptr;
                    for (u32 j = 0; j < i && !seen; j++) {
                        seen = boxes[j]->mpFont == pFont;
                    }
                    if (seen) {
                        continue;
                    }
                    if (const u32 point = firstMissing(*pFont, text); point != 0) {
                        sayNoGlyph(pMod, pPane->pane->mName, point);
                    }
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

            int32_t svcAlive(const LumaMod* pMod, LumaUiScreen screen, bool* pOut) {
                const char* const pWhere = "ui.alive";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pOut == nullptr) {
                    report(pMod, pWhere, 101u, "out", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (screen == 0 || screen > sNextHandle) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                // A handle this run gave that is not an open screen of this mod (ended, or a pane's): false.
                *pOut = screenOf(pMod, screen) != nullptr;
                return LUMA_OK;
            }

            int32_t svcSetScale(const LumaMod* pMod, LumaUiPane pane, float x, float y) {
                const char* const pWhere = "ui.set_scale";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                // NaN fails both comparisons; the infinities are past 64.
                if (!(x >= -64.0f && x <= 64.0f)) {
                    report(pMod, pWhere, 101u, "x", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (!(y >= -64.0f && y <= 64.0f)) {
                    report(pMod, pWhere, 101u, "y", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Pane* const pPane = paneOf(pMod, pane);
                if (pPane == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                // The pane's matrix is built again from its scale when it changes (Ui::Pane::CalculateMtx).
                pPane->pane->mScale = Vec2f(x, y);
                if (luma::modlog::facts()) {
                    paneFact(*pPane, "scale");
                }
                return LUMA_OK;
            }

            int32_t svcSetPosition(const LumaMod* pMod, LumaUiPane pane, float x, float y) {
                const char* const pWhere = "ui.set_position";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                // NaN fails both comparisons; the infinities are past 4096.
                if (!(x >= -4096.0f && x <= 4096.0f)) {
                    report(pMod, pWhere, 101u, "x", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (!(y >= -4096.0f && y <= 4096.0f)) {
                    report(pMod, pWhere, 101u, "y", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Pane* const pPane = paneOf(pMod, pane);
                if (pPane == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                // The translate the layout gave (and an animation's tracks write): replaced, its depth kept. The
                // pane's matrix is built again when it changes (Ui::Pane::CalculateMtxSelf).
                pPane->pane->mTranslate.x = x;
                pPane->pane->mTranslate.y = y;
                if (luma::modlog::facts()) {
                    paneFact(*pPane, "position");
                }
                return LUMA_OK;
            }

            int32_t svcMenuRows(const LumaMod* pMod, LumaUiScreen screen, const LumaUiMenuRow* pRows, uint32_t count) {
                const char* const pWhere = "ui.menu_rows";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (count > LUMA_UI_MENU_ROWS_MOST || (count != 0u && pRows == nullptr)) {
                    report(pMod, pWhere, 101u, count > LUMA_UI_MENU_ROWS_MOST ? "count" : "rows", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                Ui::Pane* found[LUMA_UI_MENU_ROWS_MOST];
                for (u32 i = 0; i < count; i++) {
                    const Pane* const pPane = paneOf(pMod, pRows[i].pane);
                    if (pPane == nullptr || pPane->screen != screen) {
                        report(pMod, pWhere, 102u, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    found[i] = pPane->pane;
                }
                Menu& rMenu = pScreen->menu;
                const s32 kept = rMenu.focus;
                clearMenu(rMenu);
                rMenu.count = count;
                for (u32 i = 0; i < count; i++) {
                    rMenu.panes[i] = found[i];
                    rMenu.on[i] = pRows[i].enabled;
                    rMenu.scale[i] = found[i]->mScale;
                    rMenu.alpha[i] = found[i]->mAlpha;
                }
                // The focus stays on its row while that row is still on (rule 4), else the first row that is on.
                rMenu.focus = kept >= 0 && kept < static_cast< s32 >(count) && rMenu.on[kept] ? kept
                              : count != 0u                                                    ? nextOn(rMenu, -1, 1)
                                                                                               : -1;
                lookMenu(rMenu);
                return LUMA_OK;
            }

            int32_t svcMenuChosen(const LumaMod* pMod, LumaUiScreen screen, int32_t* pRow) {
                const char* const pWhere = "ui.menu_chosen";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pRow == nullptr) {
                    report(pMod, pWhere, 101u, "row", nullptr);
                    return LUMA_E_ARGUMENT;
                }
                const Screen* const pScreen = screenOf(pMod, screen);
                if (pScreen == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                const Menu& rMenu = pScreen->menu;
                *pRow = rMenu.count != 0u ? rMenu.chosen : -1;
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
                svcAlive,
                svcSetScale,
                svcMenuRows,
                svcMenuChosen,
                svcSetPosition,
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

            // The newest shown screen with a menu, none while a host menu is open.
            Screen* topMenu() {
                Screen* pTop = nullptr;
                for (u32 i = 0; i < sHigh; i++) {
                    Screen& rScreen = sScreens[i];
                    if (rScreen.handle == 0 || rScreen.menu.count == 0u || rScreen.made == nullptr) {
                        continue;
                    }
                    const LayoutActor* const pActor = rScreen.made->actor;
                    if (MR::isDead(pActor) || MR::isHiddenLayout(pActor)) {
                        continue;
                    }
                    if (pTop == nullptr || rScreen.handle > pTop->handle) {
                        pTop = &rScreen;
                    }
                }
                return pTop == nullptr || NebulaSettingsMenu::isShown() ? nullptr : pTop;
            }

            // The newest shown screen with a menu takes the player's controls this frame (F's rules, 2026-11-12): the
            // game's own reading after the bindings (its pad's press edges, its pointer and touch). Nothing while a
            // host menu is open. A row taken stays menu_chosen's until the next reading: this runs after the mods'
            // frame handlers (GameSystem: services::frameStart, then ui::frame), so the next frame's handlers, the
            // step's hooks between and the mods' tasks each see it once.
            void menuFrame() {
                for (u32 i = 0; i < sHigh; i++) {
                    sScreens[i].menu.chosen = -1;
                }
                Screen* const pTop = topMenu();
                if (pTop == nullptr) {
                    return;
                }
                Menu& rMenu = pTop->menu;
                const s32 was = rMenu.focus;
                if (MR::isPressedUp(PLAYER_1)) {
                    rMenu.focus = nextOn(rMenu, rMenu.focus < 0 ? 0 : rMenu.focus, -1);
                } else if (MR::isPressedDown(PLAYER_1)) {
                    rMenu.focus = nextOn(rMenu, rMenu.focus < 0 ? -1 : rMenu.focus, 1);
                }
                // The pointer on a row that is on focuses it; on a row that is off it does nothing (rule 2).
                bool pointedOff = false;
                for (u32 i = 0; i < rMenu.count; i++) {
                    if (rMenu.panes[i]->IsVisible() &&
                        MR::isStarPointerPointingPane(pTop->made->actor, rMenu.panes[i], rMenu.panes[i]->mName, PLAYER_1, true, "弱")) {
                        if (rMenu.on[i]) {
                            rMenu.focus = static_cast< s32 >(i);
                        } else {
                            pointedOff = true;
                        }
                        break;
                    }
                }
                if (rMenu.focus != was) {
                    lookMenu(rMenu);
                }
                // A on the focused row, once a press (rule 5); never while the pointer is on a row that is off (rule 2).
                if (MR::isPressedJump(PLAYER_1) && !pointedOff && rMenu.focus >= 0 && rMenu.on[rMenu.focus]) {
                    rMenu.chosen = rMenu.focus;
                }
                // The presses were this menu's: the game's menus under it and the player read none this frame (with
                // NebulaMenuFocus::cover, which GameSystem gave it before the menus' focus moved).
                MR::getPlayerInput(PLAYER_1)->mButton->takePresses();
            }
        }  // namespace

        bool takesControls() {
            return sLive != 0 && topMenu() != nullptr;
        }

        void frame() {
            if (sLive == 0) {
                return;
            }
            menuFrame();
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
                                MR::isHiddenLayout(pActor) ? "true" : "false",
                                MR::isExecuteDrawLayout(pActor) && MR::isConnectToDrawTemporarily(pActor) ? "true" : "false");
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
