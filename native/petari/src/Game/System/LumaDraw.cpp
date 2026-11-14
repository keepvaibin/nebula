#include "Game/System/LumaDraw.hpp"

#include "Game/Render/FrameList.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/LumaFont.hpp"
#include "Game/System/LumaDrawNative.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Ui/Font.hpp"
#include "Game/Ui/Rect.hpp"
#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/WideTextWriter.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <luma_draw.h>
#include <luma_modlog.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_cooked_texture.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

// The draw service (docs/modding/services/draw.md). Game thread throughout: the calls, the points, the frame,
// the provider.
//
// A point calls the functions mods set for it, the bottom of the Mods list first, each through the kernel's call (the
// guard). Each 2-D call is one TypedDraw in the screen's units (MR::setupScreen2D: (0, 0) top left, y down, the
// game's screen width x height), written at once: what a mod draws is in this frame's list only, so nothing stays. A
// point keeps the projection, viewport, scissor and object slot it found: it saves them before the first mod and puts
// them back after the last.
//
// The 2-D path is the game's own: VERTEX_COLOR (TDDraw's mode 0) for shapes, TEX_MODULATE (the ropes' and flags') for
// images, the game's text writer (CharWriter's glyph runs, its fonts and LumaFont's) for text.
//
// No cost with no mod: point() and frame() are one load each while no mod set a function or freed a texture.

namespace luma {
    namespace draw {
        namespace {
            const u32 kPoints = LUMA_DRAW_POINTS;
            const u32 kMostMods = LUMA_MAX_MODS;
            const u32 kMostTextures = LUMA_DRAW_TEXTURES_MOST * 4u;  // across mods
            const u32 kMostFonts = LUMA_DRAW_FONTS_MOST * 4u;
            const u32 cOutsideCode = 470;
            const u32 cNotUtf8Code = 471;
            const u32 cNoGlyphCode = 472;
            const u32 cTextCutCode = 473;
            const u32 cNothingPushedCode = 474;
            const u32 cMissingTextureCode = 622;  // content's block (references 6)
            const u32 cMissingFontCode = 672;
            const u32 cScreenSize = 48u;          // LumaDrawScreen in 1.0

            bool is2D(u32 point) {
                return point >= LUMA_DRAW_HUD_BEFORE && point < kPoints;
            }

            // ---- State ----

            struct Handler {
                const LumaMod* mod;
                LumaDrawFn fn;
                void* user;
                s32 order;  // the Mods list's place when the point began (top 0)
            };

            // A mod that set a function or holds a texture or font: its counts and its frame slots.
            struct ModDraw {
                const LumaMod* mod;  // null: a free slot
                u32 slots[LUMA_DRAW_POINTS];  // Frame slots, one a point (made the first time it draws there)
                u32 frame;                    // the frame `primitives` counts
                u32 primitives;
                bool limitSaid;               // Service.Limit said this frame
                u32 textures;
                u64 textureBytes;
                u32 fonts;
                u32 lastGlyphFont;            // Draw.NoGlyph said for this font and code last (no repeat a frame)
                u32 lastGlyphCode;
            };

            struct Texture {
                LumaHandle handle;  // 0: a free slot
                const LumaMod* mod;
                GameTexture* texture;
                ResTIMG info;       // a texture by name: the record its GameTexture reads
                bool pixels;        // from pixels: the image is the GameTexture's own, handed to the renderer
                bool target;        // a view's picture (draw.view): a key the frame draws into, the slot's own
                u32 width;
                u32 height;
                u64 bytes;          // from pixels: width x height x 4
            };

            // The game's fonts by name, and a mod's own (the handle's kind).
            enum FontKind : u8 { kMessage, kMenu, kNumber, kPicture, kCinema, kModFont };
            const char* const cGameFonts[] = {"MessageFont26", "MenuFont64", "NumberFont", "PictureFont", "CinemaFont26"};

            struct FontEntry {
                LumaHandle handle;  // 0: a free slot
                const LumaMod* mod;
                FontKind kind;
                Ui::Font* font;     // a mod's own (LumaFont keeps it for the process)
                u32 hash;           // the name's
                char* name;         // malloc'd
            };

            const LumaServiceKernel* sKernel = nullptr;
            Handler sHandlers[kPoints][kMostMods];
            u32 sHandlerCount[kPoints];
            Handler sCalling[kMostMods];  // the point's copy, sorted (points never nest)
            ModDraw sMods[kMostMods];
            u32 sModHigh = 0;
            Texture sTextures[kMostTextures];
            u32 sTextureHigh = 0;
            FontEntry sFonts[kMostFonts];
            u32 sFontHigh = 0;
            LumaHandle sNextHandle = 0;
            // Freed from pixels: they leave the renderer at the next frame's update (drawn this frame still).
            GameTexture* sFreed[kMostTextures];
            bool sFreedTarget[kMostTextures];  // a view's target: its key is forgotten, not its pixels
            u32 sFreedCount = 0;
            GameTexture* sPlaceholder = nullptr;

            // The call being made: its point (kPoints: none), its mod, its stacks.
            u32 sPoint = kPoints;
            const LumaMod* sMod = nullptr;
            ModDraw* sModDraw = nullptr;
            f32 sScreenW = 608.0f;
            f32 sScreenH = 456.0f;
            struct Clip {
                f32 left, top, right, bottom;  // screen units
            };
            Clip sClips[LUMA_DRAW_STACK_MOST + 1];  // [0] the screen
            u32 sClipDepth = 0;
            // 2x3 transforms, [0] identity; each the product with the one under it.
            f32 sTransforms[LUMA_DRAW_STACK_MOST + 1][6];
            u32 sTransformDepth = 0;

            // A movie's subtitle (subtitle): its line height in the screen's units (the cinema font's own), the room
            // under it, its shadow's offset, its widest line as a share of the 4:3 frame; its frame slot (0 until the first).
            const f32 cSubtitleSize = 26.0f;
            const f32 cSubtitleBottom = 40.0f;
            const f32 cSubtitleShadow = 2.0f;
            const f32 cSubtitleWidth = 0.86f;
            u32 sSubtitleSlot = 0;

            // A text call's UTF-16 (the writer's form); static: 8 KiB is past the module's stack (no __chkstk).
            char16_t sText[LUMA_DRAW_TEXT_BYTES_MOST + 1];

            // ---- Reports ----

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1) {
                const char* args[2] = {pArg0, pArg1};
                u32 count = 0;
                while (count < 2 && args[count] != nullptr) {
                    count++;
                }
                sKernel->report(pMod, pWhere, code, args, count);
            }

            void say(const LumaMod* pMod, u32 code, const char* pRow, const char* pColumn, const char* pName0,
                     const char* pValue0, const char* pName1, const char* pValue1) {
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CONTENT;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = code;
                entry.mod = sKernel->mod_id(pMod);
                entry.file = "";
                entry.column = MR::getHashCode(pColumn);
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

            // A 2-D call: entered, and inside this mod's function at a 2-D point.
            s32 enter2D(const LumaMod* pMod, const char* pWhere) {
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (sMod != pMod || !is2D(sPoint)) {
                    report(pMod, pWhere, cOutsideCode, nullptr, nullptr);
                    return LUMA_E_STATE;
                }
                return LUMA_OK;
            }

            bool finite(f32 value) {
                return value == value && value - value == 0.0f;  // NaN and the infinities fail
            }

            s32 badArgument(const LumaMod* pMod, const char* pWhere, const char* pArgument) {
                report(pMod, pWhere, 101u, pArgument, nullptr);
                return LUMA_E_ARGUMENT;
            }

            ModDraw* modDraw(const LumaMod* pMod, bool make) {
                ModDraw* pFree = nullptr;
                for (u32 i = 0; i < sModHigh; i++) {
                    if (sMods[i].mod == pMod) {
                        return &sMods[i];
                    }
                    if (sMods[i].mod == nullptr && pFree == nullptr) {
                        pFree = &sMods[i];
                    }
                }
                if (!make) {
                    return nullptr;
                }
                if (pFree == nullptr) {
                    if (sModHigh == kMostMods) {
                        return nullptr;
                    }
                    pFree = &sMods[sModHigh++];
                }
                memset(pFree, 0, sizeof(*pFree));
                pFree->mod = pMod;
                pFree->lastGlyphCode = 0xFFFFFFFFu;
                return pFree;
            }

            // ---- The frame's 2-D units ----

            void screenNow(LumaDrawScreen& rOut, u32 point) {
                const f32 w = static_cast< f32 >(MR::getScreenWidth());
                const f32 h = static_cast< f32 >(MR::getScreenHeight());
                rOut.size = sizeof(LumaDrawScreen);
                rOut.point = point;
                rOut.frame = NebulaFrameClock::count();
                rOut.w = w;
                rOut.h = h;
                rOut.safe_w = NebulaFrameClock::cLayoutWidth < w ? NebulaFrameClock::cLayoutWidth : w;
                rOut.safe_h = h;
                rOut.safe_x = (w - rOut.safe_w) * 0.5f;
                rOut.safe_y = 0.0f;
                rOut.reserved[0] = 0;
                rOut.reserved[1] = 0;
            }

            // A point through the transform on top.
            void place(f32 x, f32 y, f32* pX, f32* pY) {
                const f32* const m = sTransforms[sTransformDepth];
                *pX = m[0] * x + m[1] * y + m[2];
                *pY = m[3] * x + m[4] * y + m[5];
            }

            // The clip on top as the scissor (fractions of the screen); false when it is empty (nothing drawn).
            bool clipOpen() {
                const Clip& rClip = sClips[sClipDepth];
                return rClip.right > rClip.left && rClip.bottom > rClip.top;
            }

            void applyClip() {
                const Clip& rClip = sClips[sClipDepth];
                if (!clipOpen()) {
                    return;
                }
                Frame::setScissor(rClip.left / sScreenW, rClip.top / sScreenH, (rClip.right - rClip.left) / sScreenW,
                                  (rClip.bottom - rClip.top) / sScreenH);
            }

            // The mod's primitives this frame, `more` with this call; false past the most (said once a frame).
            bool countFor(ModDraw* pDraw, const LumaMod* pMod, const char* pWhere, u32 more) {
                if (pDraw == nullptr) {
                    return false;
                }
                const u32 frame = NebulaFrameClock::count();
                if (pDraw->frame != frame) {
                    pDraw->frame = frame;
                    pDraw->primitives = 0;
                    pDraw->limitSaid = false;
                }
                if (pDraw->primitives + more > LUMA_DRAW_PRIMITIVES_MOST || pDraw->primitives + more < pDraw->primitives) {
                    if (!pDraw->limitSaid) {
                        pDraw->limitSaid = true;
                        char most[16];
                        nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_PRIMITIVES_MOST);
                        report(pMod, pWhere, 104u, most, nullptr);
                    }
                    return false;
                }
                pDraw->primitives += more;
                return true;
            }

            // ---- Materials ----

            // runtime/src/render/materials/vertex_color.hlsl: the vertex colour, blended by its alpha (drawn above 0).
            void shapeMaterial(SiteMaterial& rMaterial) {
                rMaterial.init(NEBULA_SHADER_VERTEX_COLOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                               NEBULA_CULL_NONE);
                rMaterial.constants[0] = 0.0f;
            }

            // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha times
            // the constant (the colour's alpha); blended by alpha.
            void imageMaterial(SiteMaterial& rMaterial, const GameTexture* pTexture, u8 alpha) {
                rMaterial.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                               NEBULA_CULL_NONE);
                rMaterial.constants[0] = alpha / 255.0f;
                rMaterial.constants[1] = 0.0f;
                rMaterial.setTexture(0, pTexture);
            }

            // A quad of the transform's corners (x0, y0) .. (x1, y1) in the mod's units, one colour.
            void quad(f32 x0, f32 y0, f32 x1, f32 y1, LumaColor colour) {
                SiteMaterial material;
                shapeMaterial(material);
                TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
                VertexPC* pVertex = site.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
                const f32 xs[4] = {x0, x1, x1, x0};
                const f32 ys[4] = {y0, y0, y1, y1};
                for (u32 i = 0; i < 4; i++) {
                    f32 x;
                    f32 y;
                    place(xs[i], ys[i], &x, &y);
                    pVertex[i] = {x, y, 0.0f, Rgba8(colour)};
                }
            }

            // The checker a missing or freed texture draws: magenta and black squares, 8 x 8 texels.
            GameTexture* placeholder() {
                if (sPlaceholder == nullptr) {
                    sPlaceholder = new GameTexture(8, 8, NEBULA_TEXTURE_LINEAR_RGBA8);
                    u8* const pImage = sPlaceholder->image();
                    for (u32 y = 0; y < 8; y++) {
                        for (u32 x = 0; x < 8; x++) {
                            u8* const p = pImage + (y * 8 + x) * 4;
                            const bool lit = ((x >> 2) ^ (y >> 2)) == 0;
                            p[0] = lit ? 255 : 0;
                            p[1] = 0;
                            p[2] = lit ? 255 : 0;
                            p[3] = 255;
                        }
                    }
                    RenderTexture::rewritten(pImage, 8 * 8 * 4);
                    sPlaceholder->setFilter(NEBULA_FILTER_NEAREST, NEBULA_FILTER_NEAREST);
                }
                return sPlaceholder;
            }

            Texture* textureOf(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < sTextureHigh; i++) {
                    if (sTextures[i].handle == handle) {
                        return sTextures[i].mod == pMod ? &sTextures[i] : nullptr;
                    }
                }
                return nullptr;
            }

            FontEntry* fontOf(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < sFontHigh; i++) {
                    if (sFonts[i].handle == handle) {
                        return sFonts[i].mod == pMod ? &sFonts[i] : nullptr;
                    }
                }
                return nullptr;
            }

            Ui::Font* fontNow(const FontEntry& rEntry) {
                switch (rEntry.kind) {
                case kMenu:
                    return MR::getMenuFont();
                case kNumber:
                    return MR::getNumberFont();
                case kPicture:
                    return MR::getPictureFont();
                case kCinema:
                    return MR::getCinemaFont();
                case kModFont:
                    return rEntry.font;
                default:
                    return MR::getFontOnCurrentLanguage();
                }
            }

            void freeTexture(Texture& rTexture) {
                if (rTexture.pixels || rTexture.target) {
                    if (sFreedCount < kMostTextures) {
                        sFreedTarget[sFreedCount] = rTexture.target;
                        sFreed[sFreedCount++] = rTexture.texture;
                    } else {
                        // Never: there are no more textures than slots. Kept rather than freed under the frame.
                        nebula_log("LumaDraw: a freed texture kept (the freed list is full)\n");
                    }
                } else {
                    delete rTexture.texture;  // the store's image stays mapped (the game's or the mod's file)
                }
                if (ModDraw* const pDraw = modDraw(rTexture.mod, false); pDraw != nullptr) {
                    pDraw->textures--;
                    pDraw->textureBytes -= rTexture.bytes;
                }
                memset(&rTexture, 0, sizeof(rTexture));
            }

            // ---- UTF-8 to the writer's UTF-16 ----

            // sText from pText: false when not UTF-8. *pCut: longer than LUMA_DRAW_TEXT_BYTES_MOST (cut there, at a
            // character's start). A character past U+FFFF (the fonts' codes are 16 bits) becomes U+FFFD.
            bool decode(const char* pText, u32* pLength, bool* pCut) {
                const u8* p = reinterpret_cast< const u8* >(pText);
                u32 at = 0;
                u32 bytes = 0;
                *pCut = false;
                while (*p != 0) {
                    u32 point = *p;
                    u32 extra = 0;
                    if (point >= 0xF0u && point <= 0xF4u) {
                        point &= 0x07u;
                        extra = 3;
                    } else if (point >= 0xE0u && point <= 0xEFu) {
                        point &= 0x0Fu;
                        extra = 2;
                    } else if (point >= 0xC2u && point <= 0xDFu) {
                        point &= 0x1Fu;
                        extra = 1;
                    } else if (point >= 0x80u) {
                        return false;  // a continuation byte first, an overlong 2-byte lead, or past F4
                    }
                    if (bytes + 1 + extra > LUMA_DRAW_TEXT_BYTES_MOST) {
                        *pCut = true;
                        break;
                    }
                    for (u32 i = 1; i <= extra; i++) {
                        if ((p[i] & 0xC0u) != 0x80u) {
                            return false;
                        }
                        point = (point << 6) | (p[i] & 0x3Fu);
                    }
                    // Overlong forms, surrogates and past U+10FFFF are not UTF-8.
                    if ((extra == 2 && point < 0x800u) || (extra == 3 && (point < 0x10000u || point > 0x10FFFFu)) ||
                        (point >= 0xD800u && point <= 0xDFFFu)) {
                        return false;
                    }
                    sText[at++] = static_cast< char16_t >(point > 0xFFFFu ? 0xFFFDu : point);
                    p += 1 + extra;
                    bytes += 1 + extra;
                }
                sText[at] = 0;
                *pLength = at;
                return true;
            }

            // Draw.NoGlyph for the first character `pFont` cannot draw (spaces and line ends need none); once for a
            // font and character in a row.
            void checkGlyphs(const LumaMod* pMod, ModDraw* pDraw, const FontEntry& rEntry, const Ui::Font* pFont, u32 length) {
                for (u32 i = 0; i < length; i++) {
                    const u32 code = sText[i];
                    if (code == '\n' || code == ' ' || pFont->HasGlyph(static_cast< Ui::CharCode >(code))) {
                        continue;
                    }
                    if (pDraw != nullptr && pDraw->lastGlyphFont == static_cast< u32 >(rEntry.handle) &&
                        pDraw->lastGlyphCode == code) {
                        return;
                    }
                    if (pDraw != nullptr) {
                        pDraw->lastGlyphFont = static_cast< u32 >(rEntry.handle);
                        pDraw->lastGlyphCode = code;
                    }
                    char found[5] = {};
                    if (code < 0x80u) {
                        found[0] = static_cast< char >(code);
                    } else if (code < 0x800u) {
                        found[0] = static_cast< char >(0xC0u | (code >> 6));
                        found[1] = static_cast< char >(0x80u | (code & 0x3Fu));
                    } else {
                        found[0] = static_cast< char >(0xE0u | (code >> 12));
                        found[1] = static_cast< char >(0x80u | ((code >> 6) & 0x3Fu));
                        found[2] = static_cast< char >(0x80u | (code & 0x3Fu));
                    }
                    char codeText[12];
                    nebula_snprintf(codeText, sizeof(codeText), "U+%04X", code);
                    char column[64];
                    nebula_snprintf(column, sizeof(column), "%llu/%u", static_cast< unsigned long long >(rEntry.handle), code);
                    say(pMod, cNoGlyphCode, "Draw.NoGlyph", column, "found", found, "code", codeText);
                    return;
                }
            }

            // The writer for `font` at `size` units a line, placed by `align`, wrapping at `wrap` (0: none).
            void setupWriter(Ui::WideTextWriter& rWriter, Ui::Font* pFont, f32 size, u32 align, f32 wrap) {
                rWriter.SetFont(*pFont);
                const f32 height = static_cast< f32 >(pFont->GetHeight());
                rWriter.SetScale(height > 0.0f ? size / height : 1.0f);
                u32 flag = Ui::WideTextWriter::VERTICAL_ORIGIN_TOP;
                if (align == LUMA_DRAW_CENTRE) {
                    flag |= Ui::WideTextWriter::HORIZONTAL_ALIGN_CENTER | Ui::WideTextWriter::HORIZONTAL_ORIGIN_CENTER;
                } else if (align == LUMA_DRAW_RIGHT) {
                    flag |= Ui::WideTextWriter::HORIZONTAL_ALIGN_RIGHT | Ui::WideTextWriter::HORIZONTAL_ORIGIN_RIGHT;
                }
                rWriter.SetDrawFlag(flag);
                if (wrap > 0.0f) {
                    rWriter.SetWidthLimit(wrap);
                }
            }

            // The text arguments common to text and text_measure: LUMA_OK with the font and sText filled.
            s32 textArguments(const LumaMod* pMod, const char* pWhere, LumaDrawFont font, const char* pText, f32 size,
                              f32 wrap, FontEntry** ppEntry, u32* pLength) {
                if (pText == nullptr) {
                    return badArgument(pMod, pWhere, "text");
                }
                if (!finite(size) || size <= 0.0f || size > 456.0f) {
                    return badArgument(pMod, pWhere, "size");
                }
                if (!finite(wrap) || wrap < 0.0f) {
                    return badArgument(pMod, pWhere, "wrap");
                }
                FontEntry* const pEntry = fontOf(pMod, font);
                if (pEntry == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                bool cut = false;
                if (!decode(pText, pLength, &cut)) {
                    report(pMod, pWhere, cNotUtf8Code, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (cut) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_TEXT_BYTES_MOST);
                    say(pMod, cTextCutCode, "Draw.TextCut", pWhere, "count", most, nullptr, nullptr);
                }
                *ppEntry = pEntry;
                return LUMA_OK;
            }

            // ---- Calls ----

            int32_t svcAt(const LumaMod* pMod, LumaDrawPoint point, LumaDrawFn fn, void* pUser) {
                const char* const pWhere = "draw.at";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                const u32 p = static_cast< u32 >(point);
                if (p >= kPoints) {
                    return badArgument(pMod, pWhere, "point");
                }
                Handler* const pList = sHandlers[p];
                u32& rCount = sHandlerCount[p];
                for (u32 i = 0; i < rCount; i++) {
                    if (pList[i].mod == pMod) {
                        if (fn != nullptr) {
                            pList[i].fn = fn;
                            pList[i].user = pUser;
                        } else {
                            pList[i] = pList[--rCount];
                        }
                        return LUMA_OK;
                    }
                }
                if (fn == nullptr) {
                    return LUMA_OK;
                }
                if (rCount == kMostMods || modDraw(pMod, true) == nullptr) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", kMostMods);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return LUMA_E_LIMIT;
                }
                pList[rCount++] = {pMod, fn, pUser, 0};
                if (luma::modlog::facts()) {
                    char mod[160];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(pMod));
                    char json[200];
                    nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"point\":%u}", mod, p);
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "draw.at.%u", p);
                    luma::modlog::fact(key, json);
                }
                return LUMA_OK;
            }

            int32_t svcScreen(const LumaMod* pMod, LumaDrawScreen* pOut) {
                const char* const pWhere = "draw.screen";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pOut == nullptr) {
                    return badArgument(pMod, pWhere, "out");
                }
                if (pOut->size < cScreenSize) {
                    char found[16];
                    nebula_snprintf(found, sizeof(found), "%u", pOut->size);
                    char expected[16];
                    nebula_snprintf(expected, sizeof(expected), "%u", cScreenSize);
                    const char* args[3] = {"LumaDrawScreen", found, expected};
                    sKernel->report(pMod, pWhere, 103u, args, 3);
                    return LUMA_E_ARGUMENT;
                }
                LumaDrawScreen screen;
                screenNow(screen, sMod == pMod ? sPoint : kPoints);
                const u32 size = pOut->size < sizeof(screen) ? pOut->size : static_cast< u32 >(sizeof(screen));
                memcpy(pOut, &screen, size);
                pOut->size = size;
                return LUMA_OK;
            }

            int32_t svcRect(const LumaMod* pMod, const LumaDrawRect* pRect, LumaColor colour) {
                const char* const pWhere = "draw.rect";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pRect == nullptr || !finite(pRect->x) || !finite(pRect->y) || !finite(pRect->w) || !finite(pRect->h) ||
                    pRect->w < 0.0f || pRect->h < 0.0f) {
                    return badArgument(pMod, pWhere, "rect");
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                if (clipOpen() && pRect->w > 0.0f && pRect->h > 0.0f) {
                    quad(pRect->x, pRect->y, pRect->x + pRect->w, pRect->y + pRect->h, colour);
                }
                return LUMA_OK;
            }

            int32_t svcRectOutline(const LumaMod* pMod, const LumaDrawRect* pRect, float thickness, LumaColor colour) {
                const char* const pWhere = "draw.rect_outline";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pRect == nullptr || !finite(pRect->x) || !finite(pRect->y) || !finite(pRect->w) || !finite(pRect->h) ||
                    pRect->w < 0.0f || pRect->h < 0.0f) {
                    return badArgument(pMod, pWhere, "rect");
                }
                if (!finite(thickness) || thickness <= 0.0f) {
                    return badArgument(pMod, pWhere, "thickness");
                }
                const f32 x0 = pRect->x;
                const f32 y0 = pRect->y;
                const f32 x1 = pRect->x + pRect->w;
                const f32 y1 = pRect->y + pRect->h;
                const f32 least = pRect->w < pRect->h ? pRect->w : pRect->h;
                const bool filled = thickness * 2.0f >= least;
                if (!countFor(sModDraw, pMod, pWhere, filled ? 1u : 4u)) {
                    return LUMA_E_LIMIT;
                }
                if (!clipOpen() || pRect->w <= 0.0f || pRect->h <= 0.0f) {
                    return LUMA_OK;
                }
                if (filled) {
                    quad(x0, y0, x1, y1, colour);
                    return LUMA_OK;
                }
                // Top and bottom across, the sides between them (no corner drawn twice: alpha stays even).
                quad(x0, y0, x1, y0 + thickness, colour);
                quad(x0, y1 - thickness, x1, y1, colour);
                quad(x0, y0 + thickness, x0 + thickness, y1 - thickness, colour);
                quad(x1 - thickness, y0 + thickness, x1, y1 - thickness, colour);
                return LUMA_OK;
            }

            int32_t svcLine(const LumaMod* pMod, LumaDrawVec2 a, LumaDrawVec2 b, float thickness, LumaColor colour) {
                const char* const pWhere = "draw.line";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!finite(a.x) || !finite(a.y)) {
                    return badArgument(pMod, pWhere, "a");
                }
                if (!finite(b.x) || !finite(b.y)) {
                    return badArgument(pMod, pWhere, "b");
                }
                if (!finite(thickness) || thickness <= 0.0f) {
                    return badArgument(pMod, pWhere, "thickness");
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                if (!clipOpen()) {
                    return LUMA_OK;
                }
                // Along the line and across it, half the thickness each way; square ends reach half past each point.
                f32 dx = b.x - a.x;
                f32 dy = b.y - a.y;
                const f32 length = __builtin_sqrtf(dx * dx + dy * dy);
                if (length > 0.0f) {
                    dx /= length;
                    dy /= length;
                } else {
                    dx = 1.0f;
                    dy = 0.0f;
                }
                const f32 half = thickness * 0.5f;
                const f32 ax = a.x - dx * half;
                const f32 ay = a.y - dy * half;
                const f32 bx = b.x + dx * half;
                const f32 by = b.y + dy * half;
                const f32 nx = -dy * half;
                const f32 ny = dx * half;
                const f32 xs[4] = {ax + nx, bx + nx, bx - nx, ax - nx};
                const f32 ys[4] = {ay + ny, by + ny, by - ny, ay - ny};
                SiteMaterial material;
                shapeMaterial(material);
                TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
                VertexPC* pVertex = site.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
                for (u32 i = 0; i < 4; i++) {
                    f32 x;
                    f32 y;
                    place(xs[i], ys[i], &x, &y);
                    pVertex[i] = {x, y, 0.0f, Rgba8(colour)};
                }
                return LUMA_OK;
            }

            int32_t svcTri(const LumaMod* pMod, const LumaDrawVec2 points[3], const LumaColor colours[3]) {
                const char* const pWhere = "draw.tri";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (points == nullptr) {
                    return badArgument(pMod, pWhere, "points");
                }
                if (colours == nullptr) {
                    return badArgument(pMod, pWhere, "colours");
                }
                for (u32 i = 0; i < 3; i++) {
                    if (!finite(points[i].x) || !finite(points[i].y)) {
                        return badArgument(pMod, pWhere, "points");
                    }
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                if (!clipOpen()) {
                    return LUMA_OK;
                }
                SiteMaterial material;
                shapeMaterial(material);
                TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
                VertexPC* pVertex = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLES, 3);
                for (u32 i = 0; i < 3; i++) {
                    f32 x;
                    f32 y;
                    place(points[i].x, points[i].y, &x, &y);
                    pVertex[i] = {x, y, 0.0f, Rgba8(colours[i])};
                }
                return LUMA_OK;
            }

            int32_t svcImage(const LumaMod* pMod, LumaDrawTexture texture, const LumaDrawRect* pDst, const LumaDrawRect* pSrc,
                             LumaColor colour) {
                const char* const pWhere = "draw.image";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pDst == nullptr || !finite(pDst->x) || !finite(pDst->y) || !finite(pDst->w) || !finite(pDst->h) ||
                    pDst->w < 0.0f || pDst->h < 0.0f) {
                    return badArgument(pMod, pWhere, "dst");
                }
                if (pSrc != nullptr && (!finite(pSrc->x) || !finite(pSrc->y) || !finite(pSrc->w) || !finite(pSrc->h))) {
                    return badArgument(pMod, pWhere, "src");
                }
                Texture* const pTexture = textureOf(pMod, texture);
                s32 status = LUMA_OK;
                const GameTexture* pDrawn = nullptr;
                f32 width = 8.0f;
                f32 height = 8.0f;
                if (pTexture == nullptr) {
                    // A freed or foreign texture: the checker, so the mistake is seen, and the report.
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    status = LUMA_E_HANDLE;
                    pDrawn = placeholder();
                } else {
                    pDrawn = pTexture->texture;
                    width = static_cast< f32 >(pTexture->width);
                    height = static_cast< f32 >(pTexture->height);
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                if (!clipOpen() || pDst->w <= 0.0f || pDst->h <= 0.0f) {
                    return status;
                }
                f32 s0 = 0.0f;
                f32 t0 = 0.0f;
                f32 s1 = 1.0f;
                f32 t1 = 1.0f;
                if (pSrc != nullptr && pTexture != nullptr) {
                    s0 = pSrc->x / width;
                    t0 = pSrc->y / height;
                    s1 = (pSrc->x + pSrc->w) / width;
                    t1 = (pSrc->y + pSrc->h) / height;
                }
                SiteMaterial material;
                imageMaterial(material, pDrawn, static_cast< u8 >(colour & 0xFFu));
                TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
                VertexPCT* pVertex = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
                const f32 xs[4] = {pDst->x, pDst->x + pDst->w, pDst->x + pDst->w, pDst->x};
                const f32 ys[4] = {pDst->y, pDst->y, pDst->y + pDst->h, pDst->y + pDst->h};
                const f32 ss[4] = {s0, s1, s1, s0};
                const f32 ts[4] = {t0, t0, t1, t1};
                const Rgba8 tint(static_cast< u8 >(colour >> 24), static_cast< u8 >(colour >> 16), static_cast< u8 >(colour >> 8), 255);
                for (u32 i = 0; i < 4; i++) {
                    f32 x;
                    f32 y;
                    place(xs[i], ys[i], &x, &y);
                    pVertex[i] = {x, y, 0.0f, tint, ss[i], ts[i]};
                }
                return status;
            }

            int32_t svcText(const LumaMod* pMod, LumaDrawFont font, const char* pText, float x, float y, float size,
                            LumaColor colour, LumaDrawAlign align, float wrap, float* pAdvance) {
                const char* const pWhere = "draw.text";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!finite(x) || !finite(y)) {
                    return badArgument(pMod, pWhere, "position");
                }
                if (static_cast< u32 >(align) > LUMA_DRAW_RIGHT) {
                    return badArgument(pMod, pWhere, "align");
                }
                FontEntry* pEntry = nullptr;
                u32 length = 0;
                if (const s32 status = textArguments(pMod, pWhere, font, pText, size, wrap, &pEntry, &length); status != LUMA_OK) {
                    return status;
                }
                Ui::Font* const pFont = fontNow(*pEntry);
                if (pAdvance != nullptr) {
                    *pAdvance = 0.0f;
                }
                if (length == 0) {
                    return LUMA_OK;
                }
                // Glyphs count as primitives: a line end or space draws none, but counting each keeps the bound plain.
                if (!countFor(sModDraw, pMod, pWhere, length)) {
                    return LUMA_E_LIMIT;
                }
                checkGlyphs(pMod, sModDraw, *pEntry, pFont, length);
                Ui::WideTextWriter writer;
                setupWriter(writer, pFont, size, static_cast< u32 >(align), wrap);
                Color8 tint;
                tint.set(static_cast< u8 >(colour >> 24), static_cast< u8 >(colour >> 16), static_cast< u8 >(colour >> 8),
                         static_cast< u8 >(colour));
                writer.SetTextColor(tint);
                writer.Setup();
                // The glyphs under the transform on top: its 2x3 as the writer's matrix (the screen's units, y down).
                const f32* const m = sTransforms[sTransformDepth];
                const f32 mtx[3][4] = {{m[0], m[1], 0.0f, m[2]}, {m[3], m[4], 0.0f, m[5]}, {0.0f, 0.0f, 1.0f, 0.0f}};
                Ui::detail::SetDrawMtx(mtx);
                writer.SetCursor(x, y);
                if (clipOpen()) {
                    (void)writer.Print(sText, static_cast< int >(length));
                } else {
                    Ui::Rect rect;
                    writer.CalcStringRect(&rect, sText, static_cast< int >(length));
                    writer.SetCursorX(x + rect.GetWidth());
                }
                if (pAdvance != nullptr) {
                    *pAdvance = writer.GetCursorX() - x;
                }
                return LUMA_OK;
            }

            int32_t svcTextMeasure(const LumaMod* pMod, LumaDrawFont font, const char* pText, float size, float wrap,
                                   LumaDrawVec2* pOut) {
                const char* const pWhere = "draw.text_measure";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pOut == nullptr) {
                    return badArgument(pMod, pWhere, "out");
                }
                FontEntry* pEntry = nullptr;
                u32 length = 0;
                if (const s32 status = textArguments(pMod, pWhere, font, pText, size, wrap, &pEntry, &length); status != LUMA_OK) {
                    return status;
                }
                pOut->x = 0.0f;
                pOut->y = 0.0f;
                if (length == 0) {
                    return LUMA_OK;
                }
                Ui::WideTextWriter writer;
                setupWriter(writer, fontNow(*pEntry), size, LUMA_DRAW_LEFT, wrap);
                Ui::Rect rect;
                writer.CalcStringRect(&rect, sText, static_cast< int >(length));
                pOut->x = rect.GetWidth();
                pOut->y = rect.GetHeight();
                return LUMA_OK;
            }

            int32_t svcClipPush(const LumaMod* pMod, const LumaDrawRect* pRect) {
                const char* const pWhere = "draw.clip_push";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pRect == nullptr || !finite(pRect->x) || !finite(pRect->y) || !finite(pRect->w) || !finite(pRect->h) ||
                    pRect->w < 0.0f || pRect->h < 0.0f) {
                    return badArgument(pMod, pWhere, "rect");
                }
                if (sClipDepth == LUMA_DRAW_STACK_MOST) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_STACK_MOST);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return LUMA_E_LIMIT;
                }
                // The box around the rectangle's corners as the transform places them, within the clip under it.
                const f32 xs[4] = {pRect->x, pRect->x + pRect->w, pRect->x + pRect->w, pRect->x};
                const f32 ys[4] = {pRect->y, pRect->y, pRect->y + pRect->h, pRect->y + pRect->h};
                Clip box = {3.4e38f, 3.4e38f, -3.4e38f, -3.4e38f};
                for (u32 i = 0; i < 4; i++) {
                    f32 x;
                    f32 y;
                    place(xs[i], ys[i], &x, &y);
                    box.left = x < box.left ? x : box.left;
                    box.top = y < box.top ? y : box.top;
                    box.right = x > box.right ? x : box.right;
                    box.bottom = y > box.bottom ? y : box.bottom;
                }
                const Clip& rUnder = sClips[sClipDepth];
                Clip& rNew = sClips[++sClipDepth];
                rNew.left = box.left > rUnder.left ? box.left : rUnder.left;
                rNew.top = box.top > rUnder.top ? box.top : rUnder.top;
                rNew.right = box.right < rUnder.right ? box.right : rUnder.right;
                rNew.bottom = box.bottom < rUnder.bottom ? box.bottom : rUnder.bottom;
                applyClip();
                return LUMA_OK;
            }

            int32_t svcClipPop(const LumaMod* pMod) {
                const char* const pWhere = "draw.clip_pop";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (sClipDepth == 0) {
                    report(pMod, pWhere, cNothingPushedCode, nullptr, nullptr);
                    return LUMA_E_STATE;
                }
                sClipDepth--;
                applyClip();
                return LUMA_OK;
            }

            int32_t svcTransformPush(const LumaMod* pMod, const float m[6]) {
                const char* const pWhere = "draw.transform_push";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (m == nullptr) {
                    return badArgument(pMod, pWhere, "m");
                }
                for (u32 i = 0; i < 6; i++) {
                    if (!finite(m[i])) {
                        return badArgument(pMod, pWhere, "m");
                    }
                }
                if (sTransformDepth == LUMA_DRAW_STACK_MOST) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_STACK_MOST);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return LUMA_E_LIMIT;
                }
                // under ∘ m: the new one applies first.
                const f32* const u = sTransforms[sTransformDepth];
                f32* const t = sTransforms[++sTransformDepth];
                t[0] = u[0] * m[0] + u[1] * m[3];
                t[1] = u[0] * m[1] + u[1] * m[4];
                t[2] = u[0] * m[2] + u[1] * m[5] + u[2];
                t[3] = u[3] * m[0] + u[4] * m[3];
                t[4] = u[3] * m[1] + u[4] * m[4];
                t[5] = u[3] * m[2] + u[4] * m[5] + u[5];
                return LUMA_OK;
            }

            int32_t svcTransformPop(const LumaMod* pMod) {
                const char* const pWhere = "draw.transform_pop";
                if (const s32 status = enter2D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (sTransformDepth == 0) {
                    report(pMod, pWhere, cNothingPushedCode, nullptr, nullptr);
                    return LUMA_E_STATE;
                }
                sTransformDepth--;
                return LUMA_OK;
            }

            int32_t svcFont(const LumaMod* pMod, const char* pName, LumaDrawFont* pOut) {
                const char* const pWhere = "draw.font";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pName == nullptr || pName[0] == 0 || strlen(pName) > 190) {
                    return badArgument(pMod, pWhere, "name");
                }
                if (pOut == nullptr) {
                    return badArgument(pMod, pWhere, "out");
                }
                const u32 hash = MR::getHashCode(pName);
                FontEntry* pFree = nullptr;
                u32 mine = 0;
                for (u32 i = 0; i < sFontHigh; i++) {
                    FontEntry& rEntry = sFonts[i];
                    if (rEntry.handle == 0) {
                        pFree = pFree == nullptr ? &rEntry : pFree;
                        continue;
                    }
                    if (rEntry.mod != pMod) {
                        continue;
                    }
                    mine++;
                    if (rEntry.hash == hash && strcmp(rEntry.name, pName) == 0) {
                        *pOut = rEntry.handle;
                        return LUMA_OK;
                    }
                }
                FontKind kind = kModFont;
                Ui::Font* pFont = nullptr;
                for (u32 i = 0; i < sizeof(cGameFonts) / sizeof(cGameFonts[0]); i++) {
                    if (strcmp(pName, cGameFonts[i]) == 0) {
                        kind = static_cast< FontKind >(i);
                    }
                }
                if (kind == kModFont) {
                    if (strncmp(pName, "Luma/", 5) != 0) {
                        say(pMod, cMissingFontCode, "Content.MissingFont", pName, "found", pName, nullptr, nullptr);
                        return LUMA_E_NOT_FOUND;
                    }
                    // LumaFont reads "<path>.brfnt" and says a missing one itself (Content.MissingFont), giving the
                    // message font in its place.
                    char file[208];
                    const u32 length = static_cast< u32 >(strlen(pName));
                    const bool hasExtension = length > 6 && strcmp(pName + length - 6, ".brfnt") == 0;
                    nebula_snprintf(file, sizeof(file), hasExtension ? "%s" : "%s.brfnt", pName);
                    pFont = luma::font::find(file);
                    if (pFont == nullptr || pFont == MR::getFontOnCurrentLanguage()) {
                        return LUMA_E_NOT_FOUND;
                    }
                }
                if (mine >= LUMA_DRAW_FONTS_MOST || (pFree == nullptr && sFontHigh == kMostFonts)) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_FONTS_MOST);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return LUMA_E_LIMIT;
                }
                char* const pCopy = static_cast< char* >(::malloc(strlen(pName) + 1));
                if (pCopy == nullptr) {
                    return LUMA_E_MEMORY;
                }
                memcpy(pCopy, pName, strlen(pName) + 1);
                FontEntry& rNew = pFree != nullptr ? *pFree : sFonts[sFontHigh++];
                rNew.handle = ++sNextHandle;
                rNew.mod = pMod;
                rNew.kind = kind;
                rNew.font = pFont;
                rNew.hash = hash;
                rNew.name = pCopy;
                if (ModDraw* const pDraw = modDraw(pMod, true); pDraw != nullptr) {
                    pDraw->fonts++;
                }
                *pOut = rNew.handle;
                return LUMA_OK;
            }

            // A free texture slot, or null after the report (LUMA_E_LIMIT) when the mod or the service has no room.
            Texture* textureSlot(const LumaMod* pMod, const char* pWhere, u64 bytes, ModDraw** ppDraw) {
                ModDraw* const pDraw = modDraw(pMod, true);
                Texture* pFree = nullptr;
                for (u32 i = 0; i < sTextureHigh && pFree == nullptr; i++) {
                    if (sTextures[i].handle == 0) {
                        pFree = &sTextures[i];
                    }
                }
                if (pFree == nullptr && sTextureHigh < kMostTextures) {
                    pFree = &sTextures[sTextureHigh++];
                }
                if (pDraw == nullptr || pFree == nullptr || pDraw->textures >= LUMA_DRAW_TEXTURES_MOST) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", LUMA_DRAW_TEXTURES_MOST);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return nullptr;
                }
                if (pDraw->textureBytes + bytes > LUMA_DRAW_TEXTURE_BYTES_MOST) {
                    report(pMod, pWhere, 104u, "67108864", nullptr);
                    return nullptr;
                }
                *ppDraw = pDraw;
                return pFree;
            }

            int32_t svcTexture(const LumaMod* pMod, const char* pName, LumaDrawTexture* pOut) {
                const char* const pWhere = "draw.texture";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pName == nullptr || pName[0] == 0 || strlen(pName) > 240) {
                    return badArgument(pMod, pWhere, "name");
                }
                if (pOut == nullptr) {
                    return badArgument(pMod, pWhere, "out");
                }
                // The store's file at the name: a texture the importer or the mod's cook wrote (NebulaTextureInfo).
                char path[248];
                nebula_snprintf(path, sizeof(path), pName[0] == '/' ? "%s" : "/%s", pName);
                const s32 entry = GameAssets::find(path);
                const NebulaTextureInfo* const pInfo =
                    entry < 0 ? nullptr : static_cast< const NebulaTextureInfo* >(GameAssets::map(entry));
                if (pInfo == nullptr || pInfo->magic != NEBULA_TEXTURE_INFO_MAGIC || pInfo->version != NEBULA_TEXTURE_INFO_VERSION) {
                    say(pMod, cMissingTextureCode, "Content.MissingTexture", pName, "found", pName, nullptr, nullptr);
                    return LUMA_E_NOT_FOUND;
                }
                ModDraw* pDraw = nullptr;
                Texture* const pSlot = textureSlot(pMod, pWhere, 0, &pDraw);
                if (pSlot == nullptr) {
                    return LUMA_E_LIMIT;
                }
                pSlot->info = ResTIMG::fromNative(*pInfo);
                pSlot->texture = new GameTexture();
                pSlot->texture->set(&pSlot->info);
                pSlot->handle = ++sNextHandle;
                pSlot->mod = pMod;
                pSlot->pixels = false;
                pSlot->width = pSlot->info.mWidth;
                pSlot->height = pSlot->info.mHeight;
                pSlot->bytes = 0;
                pDraw->textures++;
                *pOut = pSlot->handle;
                return LUMA_OK;
            }

            int32_t svcTextureFromPixels(const LumaMod* pMod, uint32_t w, uint32_t h, const void* pRgba8, uint32_t flags,
                                         LumaDrawTexture* pOut) {
                const char* const pWhere = "draw.texture_from_pixels";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (w == 0 || h == 0 || w > LUMA_DRAW_TEXTURE_SIDE_MOST || h > LUMA_DRAW_TEXTURE_SIDE_MOST) {
                    report(pMod, pWhere, 101u, w == 0 || w > LUMA_DRAW_TEXTURE_SIDE_MOST ? "w" : "h", nullptr);
                    return LUMA_E_TOO_LARGE;
                }
                if (pRgba8 == nullptr) {
                    return badArgument(pMod, pWhere, "rgba8");
                }
                if ((flags & ~(LUMA_DRAW_TEXTURE_NEAREST | LUMA_DRAW_TEXTURE_REPEAT)) != 0) {
                    return badArgument(pMod, pWhere, "flags");
                }
                if (pOut == nullptr) {
                    return badArgument(pMod, pWhere, "out");
                }
                const u64 bytes = static_cast< u64 >(w) * h * 4u;
                ModDraw* pDraw = nullptr;
                Texture* const pSlot = textureSlot(pMod, pWhere, bytes, &pDraw);
                if (pSlot == nullptr) {
                    return LUMA_E_LIMIT;
                }
                GameTexture* const pTexture = new GameTexture(w, h, NEBULA_TEXTURE_LINEAR_RGBA8);
                if (pTexture->image() == nullptr) {
                    delete pTexture;
                    return LUMA_E_MEMORY;
                }
                memcpy(pTexture->image(), pRgba8, static_cast< size_t >(bytes));
                RenderTexture::rewritten(pTexture->image(), static_cast< u32 >(bytes));
                const u8 wrap = (flags & LUMA_DRAW_TEXTURE_REPEAT) != 0 ? NEBULA_WRAP_REPEAT : NEBULA_WRAP_CLAMP;
                const u8 filter = (flags & LUMA_DRAW_TEXTURE_NEAREST) != 0 ? NEBULA_FILTER_NEAREST : NEBULA_FILTER_LINEAR;
                pTexture->setSampling(wrap, wrap, filter, filter);
                pSlot->texture = pTexture;
                pSlot->handle = ++sNextHandle;
                pSlot->mod = pMod;
                pSlot->pixels = true;
                pSlot->width = w;
                pSlot->height = h;
                pSlot->bytes = bytes;
                pDraw->textures++;
                pDraw->textureBytes += bytes;
                *pOut = pSlot->handle;
                return LUMA_OK;
            }

            int32_t svcTextureUpdate(const LumaMod* pMod, LumaDrawTexture texture, uint32_t x, uint32_t y, uint32_t w,
                                     uint32_t h, const void* pRgba8, uint32_t stride) {
                const char* const pWhere = "draw.texture_update";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Texture* const pTexture = textureOf(pMod, texture);
                if (pTexture == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (!pTexture->pixels) {
                    return badArgument(pMod, pWhere, "texture");
                }
                if (w == 0 || h == 0 || x > pTexture->width || y > pTexture->height || w > pTexture->width - x ||
                    h > pTexture->height - y) {
                    return badArgument(pMod, pWhere, "rectangle");
                }
                if (pRgba8 == nullptr) {
                    return badArgument(pMod, pWhere, "rgba8");
                }
                if (stride < w * 4u) {
                    return badArgument(pMod, pWhere, "stride");
                }
                u8* const pImage = pTexture->texture->image();
                const u8* const pFrom = static_cast< const u8* >(pRgba8);
                for (u32 row = 0; row < h; row++) {
                    memcpy(pImage + (static_cast< size_t >(y + row) * pTexture->width + x) * 4u,
                           pFrom + static_cast< size_t >(row) * stride, static_cast< size_t >(w) * 4u);
                }
                RenderTexture::rewritten(pImage, static_cast< u32 >(pTexture->bytes));
                return LUMA_OK;
            }

            int32_t svcTextureSize(const LumaMod* pMod, LumaDrawTexture texture, uint32_t* pW, uint32_t* pH) {
                const char* const pWhere = "draw.texture_size";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                const Texture* const pTexture = textureOf(pMod, texture);
                if (pTexture == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pW != nullptr) {
                    *pW = pTexture->width;
                }
                if (pH != nullptr) {
                    *pH = pTexture->height;
                }
                return LUMA_OK;
            }

            int32_t svcTextureFree(const LumaMod* pMod, LumaDrawTexture texture) {
                const char* const pWhere = "draw.texture_free";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Texture* const pTexture = textureOf(pMod, texture);
                if (pTexture == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pTexture->target) {
                    return badArgument(pMod, pWhere, "texture");  // a view's: draw.view.release frees it
                }
                freeTexture(*pTexture);
                return LUMA_OK;
            }

            // The 3-D calls (layer 3, FB): at a world point only, in the world's units under the scene's camera (a
            // TypedDraw with no place of its own), depth tested against the scene (LEQUAL; no depth written: an overlay
            // the world in front hides), blended by alpha, unlit, both sides.
            s32 enter3D(const LumaMod* pMod, const char* pWhere) {
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (sMod != pMod || is2D(sPoint) || sPoint >= kPoints) {
                    report(pMod, pWhere, cOutsideCode, nullptr, nullptr);
                    return LUMA_E_STATE;
                }
                return LUMA_OK;
            }

            bool finite3(const LumaDrawVec3* pPoint) {
                return pPoint != nullptr && finite(pPoint->x) && finite(pPoint->y) && finite(pPoint->z);
            }

            // vertex_color.hlsl in the world: the vertex colour, blended by its alpha, depth tested.
            void worldMaterial(SiteMaterial& rMaterial) {
                rMaterial.init(NEBULA_SHADER_VERTEX_COLOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                               NEBULA_CULL_NONE);
                rMaterial.constants[0] = 0.0f;
            }

            // tex_modulate.hlsl in the world: the texture times the vertex colour, its alpha times the colour's.
            void worldImageMaterial(SiteMaterial& rMaterial, const GameTexture* pTexture, u8 alpha) {
                rMaterial.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                               NEBULA_CULL_NONE);
                rMaterial.constants[0] = alpha / 255.0f;
                rMaterial.constants[1] = 0.0f;
                rMaterial.setTexture(0, pTexture);
            }

            int32_t svcLine3(const LumaMod* pMod, const LumaDrawVec3* pA, const LumaDrawVec3* pB, float thickness,
                             LumaColor colour) {
                const char* const pWhere = "draw.line3";
                if (const s32 status = enter3D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!finite3(pA)) {
                    return badArgument(pMod, pWhere, "a");
                }
                if (!finite3(pB)) {
                    return badArgument(pMod, pWhere, "b");
                }
                if (!finite(thickness) || !(thickness > 0.0f)) {
                    return badArgument(pMod, pWhere, "thickness");
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                // A quad along the line, its width across both the line and the way to the eye (it faces the camera).
                const Vec3f eye = MR::getCamPos();
                const f32 dx = pB->x - pA->x;
                const f32 dy = pB->y - pA->y;
                const f32 dz = pB->z - pA->z;
                const f32 ex = eye.x - (pA->x + pB->x) * 0.5f;
                const f32 ey = eye.y - (pA->y + pB->y) * 0.5f;
                const f32 ez = eye.z - (pA->z + pB->z) * 0.5f;
                f32 sx = dy * ez - dz * ey;
                f32 sy = dz * ex - dx * ez;
                f32 sz = dx * ey - dy * ex;
                const f32 length = std::sqrt(sx * sx + sy * sy + sz * sz);
                if (!(length > 1e-6f)) {
                    const Vec3f across = MR::getCamXdir();  // seen end on, or of no length: across the screen
                    sx = across.x;
                    sy = across.y;
                    sz = across.z;
                } else {
                    sx /= length;
                    sy /= length;
                    sz /= length;
                }
                const f32 half = thickness * 0.5f;
                sx *= half;
                sy *= half;
                sz *= half;
                SiteMaterial material;
                worldMaterial(material);
                TypedDraw site(material, nullptr);
                VertexPC* pVertex = site.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
                const Rgba8 tint(colour);
                pVertex[0] = {pA->x - sx, pA->y - sy, pA->z - sz, tint};
                pVertex[1] = {pB->x - sx, pB->y - sy, pB->z - sz, tint};
                pVertex[2] = {pB->x + sx, pB->y + sy, pB->z + sz, tint};
                pVertex[3] = {pA->x + sx, pA->y + sy, pA->z + sz, tint};
                return LUMA_OK;
            }

            int32_t svcTri3(const LumaMod* pMod, const LumaDrawVec3 points[3], const LumaColor colours[3]) {
                const char* const pWhere = "draw.tri3";
                if (const s32 status = enter3D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (points == nullptr || !finite3(&points[0]) || !finite3(&points[1]) || !finite3(&points[2])) {
                    return badArgument(pMod, pWhere, "points");
                }
                if (colours == nullptr) {
                    return badArgument(pMod, pWhere, "colours");
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                SiteMaterial material;
                worldMaterial(material);
                TypedDraw site(material, nullptr);
                VertexPC* pVertex = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLES, 3);
                for (u32 i = 0; i < 3; i++) {
                    pVertex[i] = {points[i].x, points[i].y, points[i].z, Rgba8(colours[i])};
                }
                return LUMA_OK;
            }

            // Layer 3's mesh (l3b): its arguments checked; drawing a model at a point of the frame is the next patch.
            // ---- draw.mesh (FB, layer 3b) ----
            // A model by name, made once a scene in the scene's arena (its memory goes with the scene) and drawn where a
            // mod asks at a world point: its base the matrix, its joints at rest, its own material copies with the
            // model's colours times the call's, in white light, through the service's own ItemBuffers right there.
            const u32 kMeshesMost = 32;    // models made a scene, all mods together
            const u32 kMeshName = 248;
            const u32 kMeshColours = 8;    // a material's: material 0 1, ambient 0 1, constant 0 to 3
            const u32 cMissingModelCode = 621;  // content's block (references 4)

            struct MeshModel {
                u32 hash;                  // the name's; 0: a free slot
                char name[kMeshName];
                Model* model;              // null: not in the store (said once a scene)
                NebulaColor8* colours;     // the model's own, kMeshColours a material
                u32 materials;
                LumaColor applied;         // the colour its copies hold now
            };
            MeshModel sMeshes[kMeshesMost];
            u32 sMeshCount = 0;
            ItemBuffer* sMeshOpa = nullptr;  // made once, in the permanent arena (file-scope pointers: no local statics)
            ItemBuffer* sMeshXlu = nullptr;

            NebulaColor8* meshColour(Material* pMaterial, u32 which) {
                if (which < 2) {
                    return &pMaterial->mMaterialColor[which];
                }
                if (which < 4) {
                    return &pMaterial->mAmbientColor[which - 2];
                }
                return &pMaterial->mConstants[which - 4];
            }

            // The scene's model of that name: made the first time (null: no room, no scene); its `model` null when the
            // store has none (*pNew: this call made the entry).
            MeshModel* meshOf(const char* pName, bool* pNew) {
                *pNew = false;
                const u32 hash = MR::getHashCode(pName) | 1u;
                for (u32 i = 0; i < sMeshCount; i++) {
                    if (sMeshes[i].hash == hash && strcmp(sMeshes[i].name, pName) == 0) {
                        return &sMeshes[i];
                    }
                }
                NebulaArena* const pScene = Arenas::scene();
                if (sMeshCount == kMeshesMost || pScene == nullptr) {
                    return nullptr;
                }
                MeshModel& rMesh = sMeshes[sMeshCount++];
                memset(&rMesh, 0, sizeof(rMesh));
                rMesh.hash = hash;
                memcpy(rMesh.name, pName, strlen(pName) + 1);
                rMesh.applied = 0xFFFFFFFFu;
                *pNew = true;
                if (!MR::isExistObjectOrLayoutArchive(pName)) {
                    return &rMesh;
                }
                const NebulaArena::CurrentScope scope(pScene);
                const ResourceHolder* const pHolder = MR::createAndAddResourceHolder(pName);
                // The archive by its name, the model in it by the model's: a mod's "Luma/<id>/<path>" holds <leaf>.bmd
                // (as ModelManager::init).
                const char* const pResName = MR::getArchiveModelName(pName);
                if (pHolder == nullptr || pHolder->mModelResTable == nullptr || pHolder->mModelResTable->getRes(pResName) == nullptr) {
                    return &rMesh;
                }
                Model* const pModel = MR::createModel(pHolder, pResName, ModelFlag_None);
                if (pModel == nullptr) {
                    return &rMesh;
                }
                const Vec one = {1.0f, 1.0f, 1.0f};
                pModel->setBaseScale(one);
                const u32 count = pModel->getModelData()->getMaterialCount();
                rMesh.colours = new NebulaColor8[count * kMeshColours + 1];
                for (u32 i = 0; i < count; i++) {
                    Material* const pMaterial = pModel->ownMaterial(i);
                    for (u32 k = 0; k < kMeshColours; k++) {
                        rMesh.colours[i * kMeshColours + k] = *meshColour(pMaterial, k);
                    }
                }
                rMesh.materials = count;
                rMesh.model = pModel;
                return &rMesh;
            }

            // The model's colours times `colour` (as they were at 0xFFFFFFFF), written when it changes.
            void meshColours(MeshModel& rMesh, LumaColor colour) {
                if (rMesh.applied == colour) {
                    return;
                }
                const u32 m[4] = {colour >> 24, colour >> 16 & 255u, colour >> 8 & 255u, colour & 255u};
                for (u32 i = 0; i < rMesh.materials; i++) {
                    Material* const pMaterial = rMesh.model->mMaterials[i];
                    for (u32 k = 0; k < kMeshColours; k++) {
                        const NebulaColor8& rWas = rMesh.colours[i * kMeshColours + k];
                        NebulaColor8* const pNow = meshColour(pMaterial, k);
                        pNow->r = static_cast< u8 >((rWas.r * m[0] + 127u) / 255u);
                        pNow->g = static_cast< u8 >((rWas.g * m[1] + 127u) / 255u);
                        pNow->b = static_cast< u8 >((rWas.b * m[2] + 127u) / 255u);
                        pNow->a = static_cast< u8 >((rWas.a * m[3] + 127u) / 255u);
                    }
                }
                rMesh.applied = colour;
            }

            // One draw of the model at `matrix` in the frame now (a world point: the scene's camera and projection).
            void meshDraw(MeshModel& rMesh, const float matrix[12]) {
                if (sMeshOpa == nullptr) {
                    const NebulaArena::CurrentScope scope(Arenas::permanent());
                    sMeshOpa = new ItemBuffer();
                    sMeshOpa->setNonSort();
                    sMeshXlu = new ItemBuffer();
                    sMeshXlu->setNonSort();
                }
                Model* const pModel = rMesh.model;
                Mtx base;
                for (u32 r = 0; r < 3; r++) {
                    for (u32 c = 0; c < 4; c++) {
                        base[r][c] = matrix[r * 4 + c];
                    }
                }
                pModel->setBaseTRMtx(base);
                pModel->calc();
                pModel->viewCalc();  // under ModelSys::mViewMtx: the scene's at a world point
                ItemBuffer* const pWasOpa = ModelSys::mDrawBuffer[0];
                ItemBuffer* const pWasXlu = ModelSys::mDrawBuffer[1];
                {
                    // The buffers grow in the permanent arena whatever is current at the point.
                    const NebulaArena::CurrentScope scope(Arenas::permanent());
                    ModelSys::mDrawBuffer[0] = sMeshOpa;
                    ModelSys::mDrawBuffer[1] = sMeshXlu;
                    pModel->entry();
                    ModelSys::mDrawBuffer[0] = pWasOpa;
                    ModelSys::mDrawBuffer[1] = pWasXlu;
                }
                LightFunction::loadAllLightWhite();
                sMeshOpa->draw();
                sMeshOpa->frameInit();
                sMeshXlu->draw();
                sMeshXlu->frameInit();
            }

            int32_t svcMesh(const LumaMod* pMod, const char* pModel, const float matrix[12], LumaColor colour) {
                const char* const pWhere = "draw.mesh";
                if (const s32 status = enter3D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pModel == nullptr || pModel[0] == '\0' || strlen(pModel) >= kMeshName) {
                    return badArgument(pMod, pWhere, "model");
                }
                if (matrix == nullptr) {
                    return badArgument(pMod, pWhere, "matrix");
                }
                for (u32 i = 0; i < 12; i++) {
                    if (!finite(matrix[i])) {
                        return badArgument(pMod, pWhere, "matrix");
                    }
                }
                bool made = false;
                MeshModel* const pMesh = meshOf(pModel, &made);
                if (pMesh == nullptr) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", kMeshesMost);
                    report(pMod, pWhere, 104u, most, nullptr);
                    return LUMA_E_LIMIT;
                }
                if (pMesh->model == nullptr) {
                    if (made) {
                        say(pMod, cMissingModelCode, "Content.MissingModel", pModel, "found", pModel, nullptr, nullptr);
                    }
                    return LUMA_E_NOT_FOUND;
                }
                // A primitive a material (the model's draws).
                if (!countFor(sModDraw, pMod, pWhere, pMesh->materials == 0 ? 1u : pMesh->materials)) {
                    return LUMA_E_LIMIT;
                }
                meshColours(*pMesh, colour);
                meshDraw(*pMesh, matrix);
                return LUMA_OK;
            }

            int32_t svcBillboard(const LumaMod* pMod, LumaDrawTexture texture, const LumaDrawVec3* pPos, float w, float h,
                                 LumaColor colour) {
                const char* const pWhere = "draw.billboard";
                if (const s32 status = enter3D(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!finite3(pPos)) {
                    return badArgument(pMod, pWhere, "pos");
                }
                if (!finite(w) || !finite(h) || w < 0.0f || h < 0.0f) {
                    return badArgument(pMod, pWhere, "size");
                }
                Texture* const pTexture = textureOf(pMod, texture);
                s32 status = LUMA_OK;
                const GameTexture* pDrawn = nullptr;
                if (pTexture == nullptr) {
                    // A freed or foreign texture: the checker, so the mistake is seen, and the report (as image).
                    report(pMod, pWhere, 102u, nullptr, nullptr);
                    status = LUMA_E_HANDLE;
                    pDrawn = placeholder();
                } else {
                    pDrawn = pTexture->texture;
                }
                if (!countFor(sModDraw, pMod, pWhere, 1)) {
                    return LUMA_E_LIMIT;
                }
                if (w <= 0.0f || h <= 0.0f) {
                    return status;
                }
                // Facing the camera: across its x and y in the world.
                const Vec3f right = MR::getCamXdir();
                const Vec3f up = MR::getCamYdir();
                const f32 rx = right.x * w * 0.5f, ry = right.y * w * 0.5f, rz = right.z * w * 0.5f;
                const f32 ux = up.x * h * 0.5f, uy = up.y * h * 0.5f, uz = up.z * h * 0.5f;
                SiteMaterial material;
                worldImageMaterial(material, pDrawn, static_cast< u8 >(colour & 0xFFu));
                TypedDraw site(material, nullptr);
                VertexPCT* pVertex = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
                const Rgba8 tint(static_cast< u8 >(colour >> 24), static_cast< u8 >(colour >> 16), static_cast< u8 >(colour >> 8), 255);
                pVertex[0] = {pPos->x - rx + ux, pPos->y - ry + uy, pPos->z - rz + uz, tint, 0.0f, 0.0f};
                pVertex[1] = {pPos->x + rx + ux, pPos->y + ry + uy, pPos->z + rz + uz, tint, 1.0f, 0.0f};
                pVertex[2] = {pPos->x + rx - ux, pPos->y + ry - uy, pPos->z + rz - uz, tint, 1.0f, 1.0f};
                pVertex[3] = {pPos->x - rx - ux, pPos->y - ry - uy, pPos->z - rz - uz, tint, 0.0f, 1.0f};
                return status;
            }

            const LumaDrawService cTable = {
                {sizeof(LumaDrawService), LUMA_DRAW_MAJOR, LUMA_DRAW_MINOR},
                svcAt,
                svcScreen,
                svcRect,
                svcRectOutline,
                svcLine,
                svcTri,
                svcImage,
                svcText,
                svcTextMeasure,
                svcClipPush,
                svcClipPop,
                svcTransformPush,
                svcTransformPop,
                svcFont,
                svcTexture,
                svcTextureFromPixels,
                svcTextureUpdate,
                svcTextureSize,
                svcTextureFree,
                svcLine3,
                svcTri3,
                svcMesh,
                svcBillboard,
            };

            // The service's rows (services-registry 11: draw's block, 470-569).
            const LumaServiceRow cRows[] = {
                {cOutsideCode, "Draw.Outside", {"where", nullptr, nullptr, nullptr}},
                {cNotUtf8Code, "Draw.NotUtf8", {"where", nullptr, nullptr, nullptr}},
                {cNothingPushedCode, "Draw.NothingPushed", {"where", nullptr, nullptr, nullptr}},
            };

            // ---- A point's call through the kernel (the guard names "draw event <point>") ----

            struct PointCall {
                LumaDrawFn fn;
                void* user;
                const LumaDrawScreen* screen;
            };

            void pointTrampoline(void* pUser, const LumaEvent* pEvent) {
                (void)pEvent;
                const PointCall* const pCall = static_cast< const PointCall* >(pUser);
                pCall->fn(pCall->user, pCall->screen);
            }

            // ---- The provider ----

            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                for (u32 p = 0; p < kPoints; p++) {
                    for (u32 i = 0; i < sHandlerCount[p];) {
                        if (sHandlers[p][i].mod == pMod) {
                            sHandlers[p][i] = sHandlers[p][--sHandlerCount[p]];
                        } else {
                            i++;
                        }
                    }
                }
                for (u32 i = 0; i < sTextureHigh; i++) {
                    if (sTextures[i].handle != 0 && sTextures[i].mod == pMod) {
                        freeTexture(sTextures[i]);
                    }
                }
                for (u32 i = 0; i < sFontHigh; i++) {
                    if (sFonts[i].handle != 0 && sFonts[i].mod == pMod) {
                        ::free(sFonts[i].name);
                        memset(&sFonts[i], 0, sizeof(sFonts[i]));
                    }
                }
                if (ModDraw* const pDraw = modDraw(pMod, false); pDraw != nullptr) {
                    pDraw->mod = nullptr;
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_DRAW_NAME,
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

        void subtitle(const char16_t* pText) {
            if (pText == nullptr || pText[0] == 0) {
                return;
            }
            Ui::Font* const pFont = MR::getCinemaFont();
            if (pFont == nullptr) {
                return;
            }
            // The length as the game measures a message: a tag's data may hold a zero unit, so the words end where the
            // tags say, not at the first zero.
            u32 length = static_cast< u32 >(MR::getStringLengthWithMessageTag(pText));
            if (length > LUMA_DRAW_TEXT_BYTES_MOST) {
                length = LUMA_DRAW_TEXT_BYTES_MOST;
            }
            LumaDrawScreen screen;
            screenNow(screen, LUMA_DRAW_HUD_AFTER);
            MR::setupScreen2D();
            if (sSubtitleSlot == 0) {
                sSubtitleSlot = Frame::newSlot();
            }
            Frame::setObject(sSubtitleSlot, 0);
            Ui::WideTextWriter writer;
            setupWriter(writer, pFont, cSubtitleSize, LUMA_DRAW_CENTRE, screen.safe_w * cSubtitleWidth);
            // The game's message tags (a colour, a wait, a sound) in the words take no room and draw nothing.
            MessageTagSkipTagProcessor tags;
            writer.SetTagProcessor(&tags);
            Ui::Rect rect;
            writer.CalcStringRect(&rect, pText, static_cast< int >(length));
            const f32 x = screen.w * 0.5f;
            const f32 y = screen.h - cSubtitleBottom - rect.GetHeight();
            const f32 mtx[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
            Ui::detail::SetDrawMtx(mtx);
            // The shadow, then the words over it.
            for (u32 pass = 0; pass < 2; pass++) {
                Color8 tint;
                if (pass == 0) {
                    tint.set(0, 0, 0, 176);
                } else {
                    tint.set(255, 255, 255, 255);
                }
                const f32 offset = pass == 0 ? cSubtitleShadow : 0.0f;
                writer.SetTextColor(tint);
                writer.Setup();
                writer.SetCursor(x + offset, y + offset);
                (void)writer.Print(pText, static_cast< int >(length));
            }
        }

        void point(u32 point) {
            luma::draw::native::point(point);  // draw.native's functions here first (one load with none)
            if (point >= kPoints || sHandlerCount[point] == 0 || sKernel == nullptr) {
                return;
            }
            // Bottom of the Mods list first: the top one draws over.
            const u32 count = sHandlerCount[point];
            for (u32 i = 0; i < count; i++) {
                sCalling[i] = sHandlers[point][i];
                sCalling[i].order = sKernel->mod_order(sCalling[i].mod);
            }
            for (u32 i = 1; i < count; i++) {
                const Handler moving = sCalling[i];
                u32 j = i;
                while (j > 0 && sCalling[j - 1].order < moving.order) {
                    sCalling[j] = sCalling[j - 1];
                    j--;
                }
                sCalling[j] = moving;
            }
            f32 projection[7];
            Frame::getProjectionv(projection);
            const bool flat = is2D(point);
            LumaDrawScreen screen;
            screenNow(screen, point);
            sScreenW = screen.w;
            sScreenH = screen.h;
            if (flat) {
                MR::setupScreen2D();
            }
            sPoint = point;
            for (u32 i = 0; i < count; i++) {
                const Handler& rHandler = sCalling[i];
                if (sKernel->mod_live(rHandler.mod) == 0) {
                    continue;
                }
                ModDraw* const pDraw = modDraw(rHandler.mod, true);
                if (pDraw != nullptr) {
                    if (pDraw->slots[point] == 0) {
                        pDraw->slots[point] = Frame::newSlot();
                    }
                    Frame::setObject(pDraw->slots[point], 0);
                }
                sMod = rHandler.mod;
                sModDraw = pDraw;
                sClipDepth = 0;
                sClips[0] = {0.0f, 0.0f, screen.w, screen.h};
                sTransformDepth = 0;
                const f32 identity[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
                memcpy(sTransforms[0], identity, sizeof(identity));
                PointCall call = {rHandler.fn, rHandler.user, &screen};
                LumaEvent event = {};
                event.size = sizeof(LumaEvent);
                event.kind = point;
                event.status = LUMA_OK;
                (void)sKernel->call(rHandler.mod, LUMA_DRAW_NAME, pointTrampoline, &call, &event);
                // What it left pushed is let go: the next mod starts from the screen.
                if (sClipDepth != 0 && flat) {
                    MR::setDefaultViewportAndScissor();
                }
            }
            sPoint = kPoints;
            sMod = nullptr;
            sModDraw = nullptr;
            sClipDepth = 0;
            sTransformDepth = 0;
            Frame::setProjectionv(projection);
            if (flat) {
                MR::setDefaultViewportAndScissor();
            }
            Frame::setObject(0, 0);
        }

        bool isLoading() {
            GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
            const GameSystemSceneController* const pController = pSystem != nullptr ? pSystem->mSceneController : nullptr;
            return pController != nullptr && pController->mCurrSceneControlInfo.mScene != SceneKind_Boot &&
                   pController->mCurrSceneControlInfo.mScene != SceneKind_None && !pController->isNerveNormal();
        }

        void frame() {
            if (sFreedCount == 0) {
                return;
            }
            for (u32 i = 0; i < sFreedCount; i++) {
                GameTexture* const pTexture = sFreed[i];
                const uintptr_t start = reinterpret_cast< uintptr_t >(pTexture->image());
                // A target's image is its key, four by four L8 (GameTexture::createPixelTarget).
                RenderTexture::forget(start, start + (sFreedTarget[i] ? RenderTexture::linearSize(NEBULA_TEXTURE_LINEAR_L8, 4, 4, 1)
                                                                       : RenderTexture::linearSize(NEBULA_TEXTURE_LINEAR_RGBA8, pTexture->getWidth(),
                                                                                                   pTexture->getHeight(), 1)));
                delete pTexture;
            }
            sFreedCount = 0;
        }

        void released(const void* pStart, const void* pEnd) {
            // The models made in the scene's arena go with it: the table forgets them (none drawn after).
            u32 kept = 0;
            for (u32 i = 0; i < sMeshCount; i++) {
                const void* const p = sMeshes[i].model != nullptr ? static_cast< const void* >(sMeshes[i].model)
                                                                  : static_cast< const void* >(nullptr);
                if (p == nullptr || (p >= pStart && p < pEnd)) {
                    continue;  // its model is going, or none was found (said again next scene)
                }
                if (kept != i) {
                    sMeshes[kept] = sMeshes[i];
                }
                kept++;
            }
            sMeshCount = kept;
        }

        const LumaMod* inWorld() {
            return sPoint < LUMA_DRAW_HUD_BEFORE ? sMod : nullptr;
        }

        bool count(const LumaMod* pMod, const char* pWhere, u32 more) {
            return pMod == sMod && countFor(sModDraw, pMod, pWhere, more);
        }

        s32 adoptTarget(const LumaMod* pMod, const char* pWhere, GameTexture* pTexture, u32 w, u32 h, LumaHandle* pOut) {
            const u64 bytes = static_cast< u64 >(w) * h * 4u;
            ModDraw* pDraw = nullptr;
            Texture* const pSlot = textureSlot(pMod, pWhere, bytes, &pDraw);
            if (pSlot == nullptr) {
                delete pTexture;
                return LUMA_E_LIMIT;
            }
            pSlot->texture = pTexture;
            pSlot->handle = ++sNextHandle;
            pSlot->mod = pMod;
            pSlot->pixels = false;
            pSlot->target = true;
            pSlot->width = w;
            pSlot->height = h;
            pSlot->bytes = bytes;
            pDraw->textures++;
            pDraw->textureBytes += bytes;
            *pOut = pSlot->handle;
            return LUMA_OK;
        }

        GameTexture* targetOf(const LumaMod* pMod, LumaHandle handle) {
            const Texture* const pTexture = textureOf(pMod, handle);
            return pTexture != nullptr && pTexture->target ? pTexture->texture : nullptr;
        }

        void dropTarget(const LumaMod* pMod, LumaHandle handle) {
            Texture* const pTexture = textureOf(pMod, handle);
            if (pTexture != nullptr && pTexture->target) {
                freeTexture(*pTexture);
            }
        }

        const LumaServiceProvider* provider() {
            return &cProvider;
        }
    }  // namespace draw
}  // namespace luma
