#include "Game/System/LumaDrawLight.hpp"

#include "Game/Map/LightDirector.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <luma_draw_light.h>
#include <luma_modlog.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_frame_list.h>

// The draw.light service (docs/modding/services/draw.md). Game thread throughout: the calls, the provider, and the
// frame (GameLightSignal::frame at GameScene::draw3D's begin).
//
// A light is a slot of one table across mods. Each frame drawn in a scene, each live light is
//  - a lamp of the scene: a world light of kind REQUEST (the kind every actor's lamp is), `reach` its half distance;
//  - a request for the game's one point light (LightPointCtrl::requestPointLightBy, keyed by its slot): lit when it is
//    the nearest asking to the player, blended to over the game's half second, as an actor's lamp.
// Intensity: the game's lamp falloff halves its light at h = 15 sqrt(b / (1 - b)) (reference distance 15); the traced
// emitter takes candela = 37.5 (h / 100)^2 (rt_walk.cpp, a surface at h well lit). So h = 100 sqrt(candela / 37.5) and
// b = h^2 / (h^2 + 225), which LightPointCtrl clamps to 0.95..0.999999 as it does the game's own (h 65 to 15000).
// The RT branch's point lights (NebulaFramePointLight: candela, radius) take the light's candela, radius and casts
// when that list is on this tree; until then the world light carries its place, colour and reach.
//
// No cost with no mod: frame() is one load while no light lives.

namespace luma {
    namespace draw {
        namespace light {
            namespace {
                const u32 kMost = LUMA_DRAW_LIGHTS_ALL_MOST;
                const f32 cReference = 15.0f;    // LightPointCtrl's reference distance
                const f32 cCandelaAtMetre = 37.5f; // a lamp halving at 1 m (rt_walk.cpp)
                const f32 cFar = 3.0e38f;        // no player: as far as any

                struct Light {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    f32 position[3];
                    f32 colour[3];
                    f32 candela;
                    f32 radius;  // the default taken
                    u32 flags;
                    u32 facts;   // frames given as facts (harness), up to 120
                };

                const LumaServiceKernel* sKernel = nullptr;
                Light sLights[kMost];
                u32 sHigh = 0;  // slots below are in use or were
                u32 sLive = 0;  // live lights
                LumaHandle sNextHandle = 0;

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

                bool inWorld(f32 value) {
                    // A NaN fails both.
                    return value >= -LUMA_DRAW_LIGHT_FAR && value <= LUMA_DRAW_LIGHT_FAR;
                }

                // The first field of `rDesc` outside its range, or null.
                const char* wrongOf(const LumaDrawLightDesc& rDesc) {
                    for (u32 i = 0; i < 3; i++) {
                        if (!inWorld(rDesc.position[i])) {
                            return "position";
                        }
                    }
                    for (u32 i = 0; i < 3; i++) {
                        if (!(rDesc.colour[i] >= 0.0f && rDesc.colour[i] <= 1.0f)) {
                            return "colour";
                        }
                    }
                    if (!(rDesc.candela > 0.0f && rDesc.candela <= LUMA_DRAW_LIGHT_CANDELA_MOST)) {
                        return "candela";
                    }
                    if (!(rDesc.radius == 0.0f ||
                          (rDesc.radius >= LUMA_DRAW_LIGHT_RADIUS_LEAST && rDesc.radius <= LUMA_DRAW_LIGHT_RADIUS_MOST))) {
                        return "radius";
                    }
                    if ((rDesc.flags & ~static_cast< u32 >(LUMA_DRAW_LIGHT_CASTS)) != 0) {
                        return "flags";
                    }
                    return nullptr;
                }

                // A desc the mod gave: its size (1.0's fields at least), then its values. LUMA_OK or the refusal,
                // reported.
                s32 checkDesc(const LumaMod* pMod, const char* pWhere, const LumaDrawLightDesc* pDesc) {
                    if (pDesc == nullptr) {
                        report(pMod, pWhere, 101u, "desc", nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    if (pDesc->size < sizeof(LumaDrawLightDesc)) {
                        char found[16];
                        char expected[16];
                        nebula_snprintf(found, sizeof(found), "%u", pDesc->size);
                        nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaDrawLightDesc)));
                        // Service.StructSize {where, struct, found, expected}: four arguments.
                        const char* args[3] = {"LumaDrawLightDesc", found, expected};
                        sKernel->report(pMod, pWhere, 103u, args, 3);
                        return LUMA_E_ARGUMENT;
                    }
                    if (const char* const pWrong = wrongOf(*pDesc); pWrong != nullptr) {
                        report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    return LUMA_OK;
                }

                void take(Light& rLight, const LumaDrawLightDesc& rDesc) {
                    for (u32 i = 0; i < 3; i++) {
                        rLight.position[i] = rDesc.position[i];
                        rLight.colour[i] = rDesc.colour[i];
                    }
                    rLight.candela = rDesc.candela;
                    rLight.radius = rDesc.radius == 0.0f ? LUMA_DRAW_LIGHT_RADIUS_DEFAULT : rDesc.radius;
                    rLight.flags = rDesc.flags;
                }

                Light* lightOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sLights[i].handle == handle) {
                            return sLights[i].mod == pMod ? &sLights[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                // Where its light halves, world units.
                f32 halfOf(const Light& rLight) {
                    return 100.0f * __builtin_sqrtf(rLight.candela / cCandelaAtMetre);
                }

                // ---- Facts (harness) ----

                void lightFact(const char* pKind, const Light& rLight, const char* pMore) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "draw.light.%s%s%llu", pKind, pKind[0] != 0 ? "." : "",
                                    static_cast< unsigned long long >(rLight.handle));
                    char mod[80];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rLight.mod));
                    char json[320];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"mod\":%s,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"candela\":%.3f,\"radius\":%.3f,\"half\":%.3f,"
                                    "\"casts\":%s%s}",
                                    mod, static_cast< double >(rLight.position[0]), static_cast< double >(rLight.position[1]),
                                    static_cast< double >(rLight.position[2]), static_cast< double >(rLight.candela),
                                    static_cast< double >(rLight.radius), static_cast< double >(halfOf(rLight)),
                                    (rLight.flags & LUMA_DRAW_LIGHT_CASTS) != 0 ? "true" : "false", pMore);
                    luma::modlog::fact(key, json);
                }

                void end(Light& rLight, const char* pWhy) {
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "draw.light.end.%llu", static_cast< unsigned long long >(rLight.handle));
                        char json[48];
                        nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                        luma::modlog::fact(key, json);
                    }
                    rLight = Light();
                    sLive--;
                }

                // ---- The service ----

                int32_t svcAdd(const LumaMod* pMod, const LumaDrawLightDesc* pDesc, LumaDrawLight* pOut) {
                    const char* const pWhere = "draw.light.add";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (pOut == nullptr) {
                        report(pMod, pWhere, 101u, "out", nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    if (const s32 status = checkDesc(pMod, pWhere, pDesc); status != LUMA_OK) {
                        return status;
                    }
                    u32 own = 0;
                    u32 slot = kMost;
                    for (u32 i = 0; i < kMost; i++) {
                        if (sLights[i].handle == 0) {
                            if (slot == kMost) {
                                slot = i;
                            }
                            if (i >= sHigh) {
                                break;
                            }
                        } else if (sLights[i].mod == pMod) {
                            own++;
                        }
                    }
                    if (own >= LUMA_DRAW_LIGHTS_MOST || slot == kMost) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", own >= LUMA_DRAW_LIGHTS_MOST ? LUMA_DRAW_LIGHTS_MOST : kMost);
                        report(pMod, pWhere, 104u, text, nullptr, nullptr);
                        return LUMA_E_LIMIT;
                    }
                    Light& rLight = sLights[slot];
                    rLight = Light();
                    rLight.handle = ++sNextHandle;
                    rLight.mod = pMod;
                    take(rLight, *pDesc);
                    if (slot >= sHigh) {
                        sHigh = slot + 1;
                    }
                    sLive++;
                    if (luma::modlog::facts()) {
                        lightFact("add", rLight, "");
                    }
                    *pOut = rLight.handle;
                    return LUMA_OK;
                }

                int32_t svcMove(const LumaMod* pMod, LumaDrawLight light, const float* pPosition) {
                    const char* const pWhere = "draw.light.move";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Light* const pLight = lightOf(pMod, light);
                    if (pLight == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (pPosition == nullptr || !inWorld(pPosition[0]) || !inWorld(pPosition[1]) || !inWorld(pPosition[2])) {
                        report(pMod, pWhere, 101u, "position", nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    for (u32 i = 0; i < 3; i++) {
                        pLight->position[i] = pPosition[i];
                    }
                    return LUMA_OK;
                }

                int32_t svcSet(const LumaMod* pMod, LumaDrawLight light, const LumaDrawLightDesc* pDesc) {
                    const char* const pWhere = "draw.light.set";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Light* const pLight = lightOf(pMod, light);
                    if (pLight == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (const s32 status = checkDesc(pMod, pWhere, pDesc); status != LUMA_OK) {
                        return status;
                    }
                    take(*pLight, *pDesc);
                    return LUMA_OK;
                }

                int32_t svcRemove(const LumaMod* pMod, LumaDrawLight light) {
                    const char* const pWhere = "draw.light.remove";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Light* const pLight = lightOf(pMod, light);
                    if (pLight == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    end(*pLight, "removed");
                    return LUMA_OK;
                }

                const LumaDrawLightService cTable = {
                    {sizeof(LumaDrawLightService), LUMA_DRAW_LIGHT_MAJOR, LUMA_DRAW_LIGHT_MINOR},
                    svcAdd,
                    svcMove,
                    svcSet,
                    svcRemove,
                };

                // ---- The provider ----

                // The mod's stop (or its taking out): its lights go from the next frame.
                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sLights[i].handle != 0 && sLights[i].mod == pMod) {
                            end(sLights[i], "mod");
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                // No rows of its own: the registry's (101 to 104) say everything it refuses.
                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_DRAW_LIGHT_NAME,
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
            }  // namespace

            void frame() {
                if (sLive == 0) {
                    return;
                }
                LightPointCtrl* const pPoint = MR::isExistSceneObj(SceneObj_LightDirector)
                                                   ? MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mPointCtrl
                                                   : nullptr;
                const bool player = MR::isExistSceneObj(SceneObj_MarioHolder);
                const bool facts = luma::modlog::facts();
                for (u32 i = 0; i < sHigh; i++) {
                    Light& rLight = sLights[i];
                    if (rLight.handle == 0) {
                        continue;
                    }
                    const f32 half = halfOf(rLight);
                    NebulaFrameWorldLight world = {};
                    world.id = NebulaHost::objectId(&rLight);
                    world.kind = NEBULA_WORLD_LIGHT_REQUEST;
                    for (u32 c = 0; c < 3; c++) {
                        world.color[c] = rLight.colour[c];
                        world.position[c] = rLight.position[c];
                    }
                    world.reach = half;
                    Frame::worldLight(world);
                    if (pPoint != nullptr) {
                        const Vec3f at(rLight.position[0], rLight.position[1], rLight.position[2]);
                        const f32 distance = player ? MR::calcDistanceToPlayer(at) : cFar;
                        const f32 h2 = half * half;
                        const Color8 colour(static_cast< u8 >(rLight.colour[0] * 255.0f + 0.5f),
                                            static_cast< u8 >(rLight.colour[1] * 255.0f + 0.5f),
                                            static_cast< u8 >(rLight.colour[2] * 255.0f + 0.5f), 255);
                        pPoint->requestPointLightBy(&rLight, distance, at, colour, h2 / (h2 + cReference * cReference), -1);
                    }
                    if (facts && rLight.facts < 120u) {
                        // Whether the game's point light is this one now (not traced: lit by it).
                        const bool chosen = pPoint != nullptr && pPoint->mCurrentActor == &rLight;
                        lightFact("", rLight, chosen ? ",\"chosen\":true" : ",\"chosen\":false");
                        rLight.facts++;
                    }
                }
            }

            const LumaServiceProvider* provider() { return &cProvider; }
        }
    }
}
