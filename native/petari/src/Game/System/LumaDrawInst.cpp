#include "Game/System/LumaDrawInst.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <luma_draw_inst.h>
#include <luma_modlog.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <cstdlib>
#include <cstring>

// The draw.inst service (plan 2.4 layer 5; contract draw). Game thread throughout: the calls, the provider, the frame
// (GameScene::calcAnim's end) and released (NebulaArena::destroy).
//
// An override is a slot of one table across mods: an actor, its model, and what the mod set.
//  - Colours: the model's materials become its own copies (Model::ownMaterial, made in the actor's arena so they go
//    with its scene), and each frame, after the game's own animation of them, each colour is the game's value (taken
//    again whenever the game wrote it: the value found is not the one this wrote last frame), a named parameter in
//    its place, times the override's colour. MaterialEntry::draw reads the copies when it draws.
//  - Parts: MeshEntry::Flag_ModHidden on the meshes of a joint and its children (the draw passes them over); the
//    override keeps which it hid, a bit a mesh, and clears those at its end.
//  - Scale: the model's draw pose (Model::viewCalc's capture of its joints, which the draw, the shadows and the tracer
//    read) scaled about the actor's base in the base's own axes; the joints themselves (the game's logic, its hit
//    sensors) stay the game's. A frame's pose is captured again from the joints next frame, so nothing compounds.
// The end (release, the mod's end, the actor's death) puts back what the game wrote where the copy still holds this
// override's value, and shows its hidden meshes; at the scene's end nothing is touched (its memory is going).
//
// No cost with no mod: frame() and released() are one load each while no override lives.

namespace luma {
    namespace draw {
        namespace inst {
            namespace {
                const u32 kMost = LUMA_DRAW_INSTS_ALL_MOST;
                const u32 kParamsMost = 16;     // named parameters an override keeps
                const u32 kColours8 = 8;        // material 0 1, ambient 0 1, constant 0 to 3
                const u32 kColours16 = 4;       // registers 0 to 3
                const u32 kFactFrames = 120;    // frames an override is told as a fact (harness)
                const u32 cMissingCode = 101;   // Service.Argument names what is missing (no rows of its own)
                const s32 kAllMaterials = -1;

                // What the game wrote last and what this wrote, a material.
                struct MaterialState {
                    NebulaColor8 game8[kColours8];
                    NebulaColor8 wrote8[kColours8];
                    NebulaColor16 game16[kColours16];
                    NebulaColor16 wrote16[kColours16];
                    bool seen;
                };

                struct Param {
                    s32 material;  // kAllMaterials: every one
                    u32 which;     // 0..7 an 8-bit colour, 8..11 a register
                    f32 value[4];
                };

                struct Inst {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    LiveActor* actor;
                    Model* model;
                    LumaColor colour;
                    f32 scale[3];
                    Param params[kParamsMost];
                    u32 paramCount;
                    MaterialState* materials;  // per material once a colour or parameter was set (malloc)
                    u32 materialCount;
                    u32* hidden;               // a bit a mesh this hid (malloc), or null
                    u32 meshCount;
                    bool coloured;             // colours were written and are to be kept up or put back
                    u32 scaledStamp;           // the draw pose's stamp once scaled: a pose not captured again is not
                                               // scaled twice (a model the game did not view this frame)
                    u32 facts;
                };

                const LumaServiceKernel* sKernel = nullptr;
                Inst sInsts[kMost];
                u32 sHigh = 0;
                u32 sLive = 0;
                LumaHandle sNextHandle = 0;

                const char* const cParamNames[kColours8 + kColours16] = {
                    "material0", "material1", "ambient0",  "ambient1",  "constant0", "constant1",
                    "constant2", "constant3", "register0", "register1", "register2", "register3"};

                // ---- Calls ----

                void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1) {
                    const char* args[2] = {pArg0, pArg1};
                    u32 count = 0;
                    while (count < 2 && args[count] != nullptr) {
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
                        report(pMod, pWhere, 100u, nullptr, nullptr);
                        return LUMA_E_THREAD;
                    }
                    return LUMA_OK;
                }

                Inst* instOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sInsts[i].handle == handle) {
                            return sInsts[i].mod == pMod ? &sInsts[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                // An override of this mod's from `pWhere`: LUMA_OK and *ppInst, or the refusal, reported.
                s32 enterInst(const LumaMod* pMod, const char* pWhere, LumaHandle handle, Inst** ppInst) {
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    *ppInst = instOf(pMod, handle);
                    if (*ppInst == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    return LUMA_OK;
                }

                bool finite(f32 value) {
                    return value == value && value - value == 0.0f;
                }

                // ---- Colours ----

                NebulaColor8* colour8(Material* pMaterial, u32 which) {
                    if (which < 2) {
                        return &pMaterial->mMaterialColor[which];
                    }
                    if (which < 4) {
                        return &pMaterial->mAmbientColor[which - 2];
                    }
                    return &pMaterial->mConstants[which - 4];
                }

                bool same8(const NebulaColor8& rA, const NebulaColor8& rB) {
                    return rA.r == rB.r && rA.g == rB.g && rA.b == rB.b && rA.a == rB.a;
                }

                bool same16(const NebulaColor16& rA, const NebulaColor16& rB) {
                    return rA.r == rB.r && rA.g == rB.g && rA.b == rB.b && rA.a == rB.a;
                }

                u8 byte(f32 value) {
                    const f32 clamped = value < 0.0f ? 0.0f : value > 255.0f ? 255.0f : value;
                    return static_cast< u8 >(clamped + 0.5f);
                }

                s16 tenBits(f32 value) {
                    const f32 clamped = value < -1024.0f ? -1024.0f : value > 1023.0f ? 1023.0f : value;
                    return static_cast< s16 >(clamped < 0.0f ? clamped - 0.5f : clamped + 0.5f);
                }

                // The parameter in the game's value's place for material `index`, colour `which`, or null.
                const Param* paramFor(const Inst& rInst, u32 index, u32 which) {
                    const Param* pFound = nullptr;
                    for (u32 i = 0; i < rInst.paramCount; i++) {
                        const Param& rParam = rInst.params[i];
                        if (rParam.which == which &&
                            (rParam.material == kAllMaterials || rParam.material == static_cast< s32 >(index))) {
                            pFound = &rParam;  // the latest set wins
                        }
                    }
                    return pFound;
                }

                // The materials as copies of the model's own, and their states (once).
                bool ownMaterials(Inst& rInst) {
                    if (rInst.materials != nullptr) {
                        return true;
                    }
                    ModelData* const pData = rInst.model->getModelData();
                    const u32 count = pData->getMaterialCount();
                    rInst.materials = static_cast< MaterialState* >(::calloc(count == 0 ? 1 : count, sizeof(MaterialState)));
                    if (rInst.materials == nullptr) {
                        return false;
                    }
                    rInst.materialCount = count;
                    NebulaArena* pArena = NebulaArena::findFromRoot(rInst.actor);
                    if (pArena == nullptr) {
                        pArena = NebulaArena::current();
                    }
                    const NebulaArena::CurrentScope scope(pArena);
                    for (u32 i = 0; i < count; i++) {
                        (void)rInst.model->ownMaterial(i);
                    }
                    return true;
                }

                // This frame's colours: the game's value (or the parameter), times the colour; or, `restore`, the
                // game's value back where this override's still stands.
                void writeColours(Inst& rInst, bool restore) {
                    const f32 m[4] = {static_cast< f32 >(rInst.colour >> 24) / 255.0f, static_cast< f32 >(rInst.colour >> 16 & 255u) / 255.0f,
                                      static_cast< f32 >(rInst.colour >> 8 & 255u) / 255.0f, static_cast< f32 >(rInst.colour & 255u) / 255.0f};
                    for (u32 i = 0; i < rInst.materialCount; i++) {
                        Material* const pMaterial = rInst.model->mMaterials[i];
                        MaterialState& rState = rInst.materials[i];
                        bool changed = false;
                        for (u32 k = 0; k < kColours8; k++) {
                            NebulaColor8* const pColour = colour8(pMaterial, k);
                            if (!rState.seen || !same8(*pColour, rState.wrote8[k])) {
                                rState.game8[k] = *pColour;  // the game wrote it (or it is the first look)
                            }
                            NebulaColor8 value = rState.game8[k];
                            if (!restore) {
                                f32 c[4] = {static_cast< f32 >(value.r), static_cast< f32 >(value.g), static_cast< f32 >(value.b),
                                            static_cast< f32 >(value.a)};
                                if (const Param* pParam = paramFor(rInst, i, k); pParam != nullptr) {
                                    for (u32 j = 0; j < 4; j++) {
                                        c[j] = pParam->value[j] * 255.0f;
                                    }
                                }
                                value = {byte(c[0] * m[0]), byte(c[1] * m[1]), byte(c[2] * m[2]), byte(c[3] * m[3])};
                            }
                            if (!same8(*pColour, value)) {
                                *pColour = value;
                                changed = true;
                            }
                            rState.wrote8[k] = value;
                        }
                        for (u32 k = 0; k < kColours16; k++) {
                            NebulaColor16* const pColour = &pMaterial->mRegisters[k];
                            if (!rState.seen || !same16(*pColour, rState.wrote16[k])) {
                                rState.game16[k] = *pColour;
                            }
                            NebulaColor16 value = rState.game16[k];
                            if (!restore) {
                                f32 c[4] = {static_cast< f32 >(value.r), static_cast< f32 >(value.g), static_cast< f32 >(value.b),
                                            static_cast< f32 >(value.a)};
                                if (const Param* pParam = paramFor(rInst, i, kColours8 + k); pParam != nullptr) {
                                    for (u32 j = 0; j < 4; j++) {
                                        c[j] = pParam->value[j] * 255.0f;
                                    }
                                }
                                value = {tenBits(c[0] * m[0]), tenBits(c[1] * m[1]), tenBits(c[2] * m[2]), tenBits(c[3] * m[3])};
                            }
                            if (!same16(*pColour, value)) {
                                *pColour = value;
                                changed = true;
                            }
                            rState.wrote16[k] = value;
                        }
                        rState.seen = true;
                        if (changed) {
                            pMaterial->touch();  // the renderer takes its rows again
                        }
                    }
                }

                // ---- Parts ----

                void hideJoint(Inst& rInst, Joint* pJoint, bool show) {
                    for (Material* pMaterial = pJoint->getMesh(); pMaterial != nullptr; pMaterial = pMaterial->getNext()) {
                        const u32 mesh = pMaterial->getMesh()->getIndex();
                        if (mesh >= rInst.meshCount) {
                            continue;
                        }
                        MeshEntry* const pEntry = rInst.model->getMeshEntry(mesh);
                        const u32 bit = 1u << (mesh % 32u);
                        if (show) {
                            if ((rInst.hidden[mesh / 32u] & bit) != 0) {
                                pEntry->offFlag(MeshEntry::Flag_ModHidden);
                                rInst.hidden[mesh / 32u] &= ~bit;
                            }
                        } else {
                            pEntry->onFlag(MeshEntry::Flag_ModHidden);
                            rInst.hidden[mesh / 32u] |= bit;
                        }
                    }
                    for (Joint* pChild = pJoint->mChild; pChild != nullptr; pChild = pChild->mYounger) {
                        hideJoint(rInst, pChild, show);
                    }
                }

                void showAll(Inst& rInst) {
                    if (rInst.hidden == nullptr) {
                        return;
                    }
                    for (u32 mesh = 0; mesh < rInst.meshCount; mesh++) {
                        if ((rInst.hidden[mesh / 32u] & (1u << (mesh % 32u))) != 0) {
                            rInst.model->getMeshEntry(mesh)->offFlag(MeshEntry::Flag_ModHidden);
                        }
                    }
                    ::memset(rInst.hidden, 0, (rInst.meshCount / 32u + 1u) * sizeof(u32));
                }

                // ---- Scale ----

                bool scaled(const Inst& rInst) {
                    return rInst.scale[0] != 1.0f || rInst.scale[1] != 1.0f || rInst.scale[2] != 1.0f;
                }

                // The draw pose about its base: each joint J' = A J with A = R S R^T (R the base's turn), its place
                // t' = b + A (t - b). The pose's "no scale" bits cleared: its normals are worked out again.
                void scalePose(Inst& rInst) {
                    ModelPose* const pPose = rInst.model->getDrawPose();
                    if (pPose != &rInst.model->mPose || pPose->mJointCount == 0 ||
                        (rInst.scaledStamp != 0 && pPose->mStamp == rInst.scaledStamp)) {
                        return;  // a pose another model writes (a copy, a shared one), or this one scaled already
                    }
                    const f32(*const b)[4] = pPose->mBase;
                    f32 a[3][3];
                    for (u32 r = 0; r < 3; r++) {
                        for (u32 c = 0; c < 3; c++) {
                            a[r][c] = b[r][0] * rInst.scale[0] * b[c][0] + b[r][1] * rInst.scale[1] * b[c][1] +
                                      b[r][2] * rInst.scale[2] * b[c][2];
                        }
                    }
                    for (u32 j = 0; j < pPose->mJointCount; j++) {
                        f32(*const m)[4] = pPose->mJoints[j];
                        f32 out[3][4];
                        for (u32 r = 0; r < 3; r++) {
                            for (u32 c = 0; c < 3; c++) {
                                out[r][c] = a[r][0] * m[0][c] + a[r][1] * m[1][c] + a[r][2] * m[2][c];
                            }
                            out[r][3] = b[r][3] + a[r][0] * (m[0][3] - b[0][3]) + a[r][1] * (m[1][3] - b[1][3]) +
                                        a[r][2] * (m[2][3] - b[2][3]);
                        }
                        ::memcpy(m, out, sizeof(out));
                    }
                    ::memset(pPose->mFlags, 0, (pPose->mJointCount / 32u + 1u) * sizeof(u32));
                    pPose->mStamp++;
                    rInst.scaledStamp = pPose->mStamp;
                }

                // ---- Facts (harness) ----

                void instFact(const Inst& rInst) {
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "draw.inst.%llu", static_cast< unsigned long long >(rInst.handle));
                    char mod[80];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rInst.mod));
                    u32 hidden = 0;
                    for (u32 mesh = 0; rInst.hidden != nullptr && mesh < rInst.meshCount; mesh++) {
                        hidden += (rInst.hidden[mesh / 32u] >> (mesh % 32u)) & 1u;
                    }
                    char json[320];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"mod\":%s,\"colour\":%u,\"params\":%u,\"materials\":%u,\"hidden\":%u,\"sx\":%.3f,"
                                    "\"sy\":%.3f,\"sz\":%.3f}",
                                    mod, static_cast< unsigned >(rInst.colour), rInst.paramCount, rInst.materialCount, hidden,
                                    static_cast< double >(rInst.scale[0]), static_cast< double >(rInst.scale[1]),
                                    static_cast< double >(rInst.scale[2]));
                    luma::modlog::fact(key, json);
                }

                // The override's end: `touch` false at the scene's end (its memory is going: nothing is put back).
                void end(Inst& rInst, const char* pWhy, bool touch) {
                    if (touch) {
                        if (rInst.coloured && rInst.materials != nullptr) {
                            writeColours(rInst, true);
                        }
                        showAll(rInst);
                    }
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "draw.inst.end.%llu", static_cast< unsigned long long >(rInst.handle));
                        char json[48];
                        nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                        luma::modlog::fact(key, json);
                    }
                    ::free(rInst.materials);
                    ::free(rInst.hidden);
                    rInst = Inst();
                    sLive--;
                }

                // ---- The service ----

                int32_t svcOf(const LumaMod* pMod, LumaActor actor, LumaDrawInst* pOut) {
                    const char* const pWhere = "draw.inst.of";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    LiveActor* const pActor = static_cast< LiveActor* >(actor);
                    const char* pWrong = pActor == nullptr ? "actor" : pOut == nullptr ? "out" : nullptr;
                    if (pWrong == nullptr && (!lumacore::isA(lumacore::classOf(pActor), lumacore::kClassId< LiveActor >) ||
                                              MR::isDead(pActor) || pActor->mModelManager == nullptr ||
                                              MR::getModel(pActor) == nullptr)) {
                        pWrong = "actor";
                    }
                    if (pWrong != nullptr) {
                        report(pMod, pWhere, cMissingCode, pWrong, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    u32 own = 0;
                    u32 slot = kMost;
                    for (u32 i = 0; i < kMost; i++) {
                        if (sInsts[i].handle == 0) {
                            if (slot == kMost) {
                                slot = i;
                            }
                            if (i >= sHigh) {
                                break;
                            }
                        } else if (sInsts[i].mod == pMod) {
                            if (sInsts[i].actor == pActor) {
                                *pOut = sInsts[i].handle;  // the one it has on this actor already
                                return LUMA_OK;
                            }
                            own++;
                        }
                    }
                    if (own >= LUMA_DRAW_INSTS_MOST || slot == kMost) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", own >= LUMA_DRAW_INSTS_MOST ? LUMA_DRAW_INSTS_MOST : kMost);
                        report(pMod, pWhere, 104u, text, nullptr);
                        return LUMA_E_LIMIT;
                    }
                    Inst& rInst = sInsts[slot];
                    rInst = Inst();
                    rInst.handle = ++sNextHandle;
                    rInst.mod = pMod;
                    rInst.actor = pActor;
                    rInst.model = MR::getModel(pActor);
                    rInst.colour = 0xFFFFFFFFu;
                    rInst.scale[0] = rInst.scale[1] = rInst.scale[2] = 1.0f;
                    rInst.meshCount = rInst.model->getModelData()->getMeshCount();
                    if (slot >= sHigh) {
                        sHigh = slot + 1;
                    }
                    sLive++;
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "draw.inst.of.%llu", static_cast< unsigned long long >(rInst.handle));
                        char mod[80];
                        luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(pMod));
                        char json[160];
                        nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"meshes\":%u}", mod, rInst.meshCount);
                        luma::modlog::fact(key, json);
                    }
                    *pOut = rInst.handle;
                    return LUMA_OK;
                }

                int32_t svcSetColour(const LumaMod* pMod, LumaDrawInst inst, LumaColor colour) {
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, "draw.inst.set_colour", inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    pInst->colour = colour;
                    return LUMA_OK;
                }

                int32_t svcSetMaterialParam(const LumaMod* pMod, LumaDrawInst inst, const char* pMaterial, const char* pName,
                                            const float* pValue) {
                    const char* const pWhere = "draw.inst.set_material_param";
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, pWhere, inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    u32 which = kColours8 + kColours16;
                    for (u32 i = 0; pName != nullptr && i < kColours8 + kColours16; i++) {
                        if (::strcmp(pName, cParamNames[i]) == 0) {
                            which = i;
                        }
                    }
                    if (which == kColours8 + kColours16) {
                        report(pMod, pWhere, 101u, "name", nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    const f32 least = which < kColours8 ? 0.0f : -4.0f;
                    const f32 most = which < kColours8 ? 1.0f : 4.0f;
                    for (u32 j = 0; j < 4; j++) {
                        if (pValue == nullptr || !finite(pValue[j]) || pValue[j] < least || pValue[j] > most) {
                            report(pMod, pWhere, 101u, "value", nullptr);
                            return LUMA_E_ARGUMENT;
                        }
                    }
                    s32 material = kAllMaterials;
                    if (pMaterial != nullptr) {
                        ModelData* const pData = pInst->model->getModelData();
                        for (u32 i = 0; i < pData->getMaterialCount(); i++) {
                            const char* const pFound = pData->getMaterialName(i);
                            if (pFound != nullptr && ::strcmp(pFound, pMaterial) == 0) {
                                material = static_cast< s32 >(i);
                                break;
                            }
                        }
                        if (material == kAllMaterials) {
                            report(pMod, pWhere, 101u, "material", nullptr);
                            return LUMA_E_NOT_FOUND;
                        }
                    }
                    // The same material and name again: in its place; else a new one.
                    u32 at = pInst->paramCount;
                    for (u32 i = 0; i < pInst->paramCount; i++) {
                        if (pInst->params[i].material == material && pInst->params[i].which == which) {
                            at = i;
                        }
                    }
                    if (at == kParamsMost) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", kParamsMost);
                        report(pMod, pWhere, 104u, text, nullptr);
                        return LUMA_E_LIMIT;
                    }
                    Param& rParam = pInst->params[at];
                    rParam.material = material;
                    rParam.which = which;
                    for (u32 j = 0; j < 4; j++) {
                        rParam.value[j] = pValue[j];
                    }
                    if (at == pInst->paramCount) {
                        pInst->paramCount++;
                    }
                    return LUMA_OK;
                }

                int32_t svcSetTexture(const LumaMod* pMod, LumaDrawInst inst, const char* pMaterial, uint32_t map,
                                      LumaDrawTexture texture) {
                    (void)pMaterial;
                    (void)map;
                    (void)texture;
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, "draw.inst.set_texture", inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    return LUMA_E_ABSENT;  // 1.0's contract call; a draw texture on a model's map is a later 1.x
                }

                int32_t svcSetVisible(const LumaMod* pMod, LumaDrawInst inst, const char* pPart, int32_t on) {
                    const char* const pWhere = "draw.inst.set_visible";
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, pWhere, inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    Joint* pJoint = nullptr;
                    if (pPart != nullptr) {
                        pJoint = MR::getJoint(pInst->model, pPart);
                        if (pJoint == nullptr) {
                            report(pMod, pWhere, 101u, "part", nullptr);
                            return LUMA_E_NOT_FOUND;
                        }
                    }
                    if (pInst->hidden == nullptr) {
                        pInst->hidden = static_cast< u32* >(::calloc(pInst->meshCount / 32u + 1u, sizeof(u32)));
                        if (pInst->hidden == nullptr) {
                            report(pMod, pWhere, 104u, "memory", nullptr);
                            return LUMA_E_LIMIT;
                        }
                    }
                    if (pJoint != nullptr) {
                        hideJoint(*pInst, pJoint, on != 0);
                    } else if (on != 0) {
                        showAll(*pInst);
                    } else {
                        for (u32 mesh = 0; mesh < pInst->meshCount; mesh++) {
                            pInst->model->getMeshEntry(mesh)->onFlag(MeshEntry::Flag_ModHidden);
                            pInst->hidden[mesh / 32u] |= 1u << (mesh % 32u);
                        }
                    }
                    return LUMA_OK;
                }

                int32_t svcSetScale(const LumaMod* pMod, LumaDrawInst inst, const float* pScale) {
                    const char* const pWhere = "draw.inst.set_scale";
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, pWhere, inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    for (u32 j = 0; j < 3; j++) {
                        if (pScale == nullptr || !(pScale[j] > 0.0f && pScale[j] <= LUMA_DRAW_INST_SCALE_MOST)) {
                            report(pMod, pWhere, 101u, "scale", nullptr);
                            return LUMA_E_ARGUMENT;
                        }
                    }
                    for (u32 j = 0; j < 3; j++) {
                        pInst->scale[j] = pScale[j];
                    }
                    return LUMA_OK;
                }

                int32_t svcRelease(const LumaMod* pMod, LumaDrawInst inst) {
                    Inst* pInst = nullptr;
                    if (const s32 status = enterInst(pMod, "draw.inst.release", inst, &pInst); status != LUMA_OK) {
                        return status;
                    }
                    end(*pInst, "released", true);
                    return LUMA_OK;
                }

                const LumaDrawInstService cTable = {
                    {sizeof(LumaDrawInstService), LUMA_DRAW_INST_MAJOR, LUMA_DRAW_INST_MINOR},
                    svcOf,
                    svcSetColour,
                    svcSetMaterialParam,
                    svcSetTexture,
                    svcSetVisible,
                    svcSetScale,
                    svcRelease,
                };

                // ---- The provider ----

                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sInsts[i].handle != 0 && sInsts[i].mod == pMod) {
                            end(sInsts[i], "mod", true);
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
                    LUMA_DRAW_INST_NAME,
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

                bool inRange(const void* p, const void* pStart, const void* pEnd) {
                    return p >= pStart && p < pEnd;
                }
            }  // namespace

            void frame() {
                if (sLive == 0) {
                    return;
                }
                const bool facts = luma::modlog::facts();
                for (u32 i = 0; i < sHigh; i++) {
                    Inst& rInst = sInsts[i];
                    if (rInst.handle == 0) {
                        continue;
                    }
                    if (MR::isDead(rInst.actor)) {
                        end(rInst, "actor", true);
                        continue;
                    }
                    const bool colours = rInst.colour != 0xFFFFFFFFu || rInst.paramCount != 0;
                    if (colours || rInst.coloured) {
                        if (ownMaterials(rInst)) {
                            writeColours(rInst, !colours);
                            rInst.coloured = colours;
                        }
                    }
                    if (scaled(rInst)) {
                        scalePose(rInst);
                    }
                    if (facts && rInst.facts < kFactFrames) {
                        rInst.facts++;
                        instFact(rInst);
                    }
                }
            }

            void released(const void* pStart, const void* pEnd) {
                if (sLive == 0) {
                    return;
                }
                for (u32 i = 0; i < sHigh; i++) {
                    if (sInsts[i].handle != 0 && inRange(sInsts[i].actor, pStart, pEnd)) {
                        end(sInsts[i], "scene", false);
                    }
                }
            }

            const LumaServiceProvider* provider() {
                return &cProvider;
            }
        }  // namespace inst
    }  // namespace draw
}  // namespace luma
