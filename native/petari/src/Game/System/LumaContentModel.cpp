#include "Game/System/LumaContentModel.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Table.hpp"
#include <luma.h>
#include <luma_content_model.h>
#include <luma_modlog.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_math.h>
#include <nebula_module.h>
#include <cmath>
#include <cstring>

// The content.model service (docs/modding/content/models.md section 7). Game thread throughout: the service's calls,
// the frame, the provider; NebulaArena::destroy is the scene end's, on the game thread as well.
//
// An attached model is the game's own PartsModel: an actor that draws a model with its host, made in the host's arena
// (so it goes with the host's scene), drawn by the game's draw buffers with the game's lights, hidden while its host is
// hidden or clipped (PartsModel::movement). It follows the host's base or one of its joints through the game's
// FixedPosition; a skeleton follow sets each joint's matrix from the host's joint of its name in the model's calc
// callback (Model::calc, after its own pose: a joint with no such name keeps its offset from its parent). A part made
// after the scene started connects as any late object (NameObjExecuteHolder::registerActor). A part whose handle ended
// is dead and is used again for the same host, model, follow and joint.
//
// Levels (models.md 6): the game draws an object's Low model where the level's view cube asks for its view group's far
// level (LodCtrl). An attached model is drawn with its host, so it takes its host's level: with a host whose LodCtrl
// has a far model and a model that has a Low archive (LodCtrlFunction::isExistLevel), the Low is a second part made
// with the base, following as the base does, alive exactly while the host's far model is (the host then hides
// its own model, and the base part with it). No Middle is drawn: the game draws none for any object.
//
// No cost with no mod: frame() and released() are one load each while no model was attached.

  // model.pose facts a handle writes, from its attach (harness only)

namespace luma {
    namespace content {
        namespace model {
            namespace {
                const u32 kMostModels = LUMA_CONTENT_MODEL_MOST * 16u;  // across mods
                // The placeholder model (references.md 4): Nebula's data/placeholder/model.gltf, an object archive in
                // the store.
                const char cPlaceholder[] = "NebulaPlaceholder";
                const u32 cMissingModelCode = 621;
                const u32 cMissingJointCode = 626;
                const u32 cMissingParamCode = 671;

                struct Part {
                    Part* next;        // every part, for reuse and release
                    LiveActor* host;
                    PartsModel* actor;
                    char* archive;     // the arena's copy
                    char* joint;       // the joint followed (the arena's copy); null: the base
                    u32 follow;        // LumaContentModelFollow
                    bool busy;         // an open handle uses it
                    // Skeleton follow: per joint of the model, the host's joint of its name (-1: none), its parent (-1:
                    // the root), the order parents first, and this frame's own pose before the host's replaces it.
                    const ModelData* hostData;
                    u32 joints;
                    s16* map;
                    s16* parent;
                    u16* order;
                    Mtx* own;
                    Part* low;              // the model's Low level, made with it; null: none
                    const LodCtrl* hostLod;  // the host's LodCtrl with its far model (with `low`)
                };

                struct Attached {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    LiveActor* host;
                    Part* part;         // null: neither the model nor the placeholder is in the store: nothing shows
                    u32 frame;          // frames the frame has seen it
                    bool reused;        // its part was a dead one used again (harness fact)
                    s8 level;           // the level last written as a fact: -1 none yet, 0 the base, 1 Low
                };

                const LumaServiceKernel* sKernel = nullptr;
                Attached sAttached[kMostModels];
                u32 sHigh = 0;  // slots below are in use or were
                u32 sLive = 0;  // open handles
                LumaHandle sNextHandle = 0;
                Part* sParts = nullptr;

                // The player's parts (models.md 7): models of their own the game draws at the body's joints
                // (MarioActorDraw.cpp updates each at the body joint's matrix, MarioActor.cpp 1471; MarioActorShadow.cpp).
                struct PlayerPartRow {
                    const char* name;
                    const char* joint;
                };
                const PlayerPartRow cPlayerParts[] = {
                    {"MarioHandL", "HandL"},           {"MarioHandR", "HandR"},
                    {"IceMarioHandL", "HandL"},        {"IceMarioHandR", "HandR"},
                    {"InvincibleMarioHandL", "HandL"}, {"InvincibleMarioHandR", "HandR"},
                    {"MarioFace", "Face0"},             {"LuigiFace", "Face0"},
                    {"MarioShadow", ""},                {"LuigiShadow", ""},
                };
                const u32 cPlayerPartCount = sizeof(cPlayerParts) / sizeof(cPlayerParts[0]);

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

                // A CONTENT entry of the mod (references.md 6). The column keeps two names two entries.
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

                const char* modelNameOf(const LiveActor* pActor) {
                    if (const ResourceHolder* pHolder = MR::getModelResourceHolder(pActor); pHolder != nullptr) {
                        if (const char* pModel = pHolder->getModelName(); pModel != nullptr) {
                            return pModel;
                        }
                    }
                    return lumacore::classRow(lumacore::classOf(pActor)).name;
                }

                const char* followName(u32 follow) {
                    return follow == LUMA_CONTENT_MODEL_JOINT ? "joint" : follow == LUMA_CONTENT_MODEL_SKELETON ? "skeleton" : "actor";
                }

                bool inRange(const void* p, const void* pStart, const void* pEnd) {
                    const uintptr_t at = reinterpret_cast< uintptr_t >(p);
                    return at >= reinterpret_cast< uintptr_t >(pStart) && at < reinterpret_cast< uintptr_t >(pEnd);
                }

                bool sameText(const char* pA, const char* pB) {
                    if (pA == nullptr || pB == nullptr) {
                        return pA == pB;
                    }
                    return strcmp(pA, pB) == 0;
                }

                // A new model's level block (models.md 6): "<id>:<path>Middle" or "<id>:<path>Low" in the store beside
                // its base "<id>:<path>". It is a level of that model, not a model to attach. The endings are matched
                // with their capitals and need a name before them in the last segment, as the cook finds levels
                // (overlay.rs level_alone). A game archive's name (no ':') is never one: the game's own Low archives
                // are models.
                bool isLevelBlock(const char* pName) {
                    if (strchr(pName, ':') == nullptr) {
                        return false;
                    }
                    const size_t length = strlen(pName);
                    const char* const cEndings[] = {"Middle", "Low"};
                    for (const char* pEnding : cEndings) {
                        const size_t ending = strlen(pEnding);
                        if (length <= ending || strcmp(pName + length - ending, pEnding) != 0) {
                            continue;
                        }
                        const char before = pName[length - ending - 1];
                        if (before == ':' || before == '/') {
                            return false;
                        }
                        const MR::FormattedName base("%.*s", static_cast< int >(length - ending), pName);
                        return MR::isExistObjectOrLayoutArchive(pName) && MR::isExistObjectOrLayoutArchive(base.c_str());
                    }
                    return false;
                }

                char* copyText(const char* pText) {
                    if (pText == nullptr) {
                        return nullptr;
                    }
                    const size_t length = strlen(pText);
                    char* const pCopy = new char[length + 1];
                    memcpy(pCopy, pText, length + 1);
                    return pCopy;
                }

                // ---- Skeleton follow ----

                // Model::calc's callback, after the model's own pose: each joint takes the host's joint of its name; one
                // with none keeps its offset from its parent (own parent^-1 x own, under the parent's new matrix).
                void followSkeleton(Model* pModel, u32 view) {
                    (void)view;
                    Part* const pPart = reinterpret_cast< Part* >(pModel->mUserArea);
                    if (pPart == nullptr || pPart->map == nullptr) {
                        return;
                    }
                    const ModelManager* const pHostManager = pPart->host->mModelManager;
                    if (pHostManager == nullptr || MR::getModelData(pPart->host) != pPart->hostData) {
                        return;  // the host draws another model now (a form's): the part keeps its own pose
                    }
                    Model* const pHost = MR::getModel(pPart->host);
                    for (u32 k = 0; k < pPart->joints; k++) {
                        const u32 i = pPart->order[k];
                        NebulaMtxCopy(pModel->getAnmMtx(static_cast< int >(i)), pPart->own[i]);
                        const s16 hostJoint = pPart->map[i];
                        if (hostJoint >= 0) {
                            pModel->setAnmMtx(static_cast< int >(i), pHost->getAnmMtx(hostJoint));
                            continue;
                        }
                        const s16 parent = pPart->parent[i];
                        if (parent < 0) {
                            continue;  // the root with no name on the host stays on the host's base, as posed
                        }
                        Mtx inverse;
                        Mtx local;
                        Mtx moved;
                        if (NebulaMtxInverse(pPart->own[parent], inverse) == 0) {
                            continue;
                        }
                        NebulaMtxConcat(inverse, pPart->own[i], local);
                        NebulaMtxConcat(pModel->getAnmMtx(parent), local, moved);
                        pModel->setAnmMtx(static_cast< int >(i), moved);
                    }
                }

                // The tables followSkeleton reads, in the part's arena (the scope is open).
                void makeSkeletonTables(Part* pPart) {
                    const ModelData* const pData = MR::getModelData(pPart->actor);
                    const ModelData* const pHostData = MR::getModelData(pPart->host);
                    const u32 count = pData->getJointCount();
                    pPart->hostData = pHostData;
                    pPart->joints = count;
                    pPart->map = new s16[count];
                    pPart->parent = new s16[count];
                    pPart->order = new u16[count];
                    pPart->own = new Mtx[count];
                    for (u32 i = 0; i < count; i++) {
                        pPart->map[i] = static_cast< s16 >(pHostData->findJoint(pData->getJointName(i)));
                        pPart->parent[i] = -1;
                    }
                    // Parents first: a walk of the joint tree from joint 0 (children, then younger siblings), with the
                    // order array as its stack's output; joints the walk does not reach keep their own pose.
                    u32 placed = 0;
                    if (count != 0) {
                        pPart->order[placed++] = 0;
                        for (u32 k = 0; k < placed; k++) {
                            Joint* const pJoint = pData->getJointAt(pPart->order[k]);
                            for (Joint* pChild = pJoint->getChild(); pChild != nullptr && placed < count; pChild = pChild->getYounger()) {
                                const u32 child = pChild->getJntNo();
                                if (child < count && child != 0 && pPart->parent[child] < 0) {
                                    pPart->parent[child] = static_cast< s16 >(pPart->order[k]);
                                    pPart->order[placed++] = static_cast< u16 >(child);
                                }
                            }
                        }
                    }
                    pPart->joints = placed;
                }

                // ---- Parts ----

                // A part of `pArchive` drawn with `pHost`, in the open arena scope.
                Part* makePart(LiveActor* pHost, const char* pArchive, u32 follow, const char* pJoint, const Vec3f& rOffset,
                               const Vec3f& rTurn) {
                    Part* const pPart = new Part();
                    pPart->next = nullptr;
                    pPart->host = pHost;
                    pPart->archive = copyText(pArchive);
                    pPart->joint = copyText(pJoint);
                    pPart->follow = follow;
                    pPart->busy = false;
                    pPart->hostData = nullptr;
                    pPart->joints = 0;
                    pPart->map = nullptr;
                    pPart->parent = nullptr;
                    pPart->order = nullptr;
                    pPart->own = nullptr;
                    pPart->low = nullptr;
                    pPart->hostLod = nullptr;
                    // With the player, its decorations' lists (after the player's pose); else the map objects' (after
                    // every enemy's, NPC's and object's pose: SceneFunction::executeCalcAnimList).
                    const int drawBuffer = pHost == MR::getPlayerDemoActor() ? static_cast< int >(MR::DrawBufferType_PlayerDecoration) : -1;
                    PartsModel* const pActor = new PartsModel(pHost, "LumaCore model", pPart->archive, nullptr, drawBuffer, false);
                    pPart->actor = pActor;
                    // The skeleton's callback before the part first appears: makeActorAppeared poses it at once
                    // (MR::resetPosition), and that pose is the one drawn the frame it is attached.
                    if (follow == LUMA_CONTENT_MODEL_SKELETON) {
                        makeSkeletonTables(pPart);
                        Model* const pModel = MR::getModel(pActor);
                        pModel->mUserArea = reinterpret_cast< uintptr_t >(pPart);
                        pModel->mCalcCallBack = followSkeleton;
                    }
                    pActor->initFixedPosition(rOffset, rTurn, pPart->joint);
                    pActor->initWithoutIter();
                    MR::initLightCtrl(pActor);
                    return pPart;
                }

                // A dead part for this host, model, follow and joint, or a new one, placed by `rOffset` and `rTurn`.
                Part* partFor(LiveActor* pHost, const char* pArchive, u32 follow, const char* pJoint, const Vec3f& rOffset,
                              const Vec3f& rTurn, bool* pReused) {
                    *pReused = false;
                    for (Part* p = sParts; p != nullptr; p = p->next) {
                        if (!p->busy && p->host == pHost && p->follow == follow && strcmp(p->archive, pArchive) == 0 &&
                            sameText(p->joint, pJoint)) {
                            p->actor->mFixedPosition->mLocalTrans = rOffset;
                            p->actor->mFixedPosition->mLocalRotate = rTurn;
                            p->actor->makeActorAppeared();
                            MR::showModelIfHidden(p->actor);
                            if (p->low != nullptr) {
                                // Dead until the frame finds the host's far model drawn.
                                p->low->actor->mFixedPosition->mLocalTrans = rOffset;
                                p->low->actor->mFixedPosition->mLocalRotate = rTurn;
                                MR::showModelIfHidden(p->low->actor);
                            }
                            *pReused = true;
                            return p;
                        }
                    }
                    NebulaArena* pArena = NebulaArena::findFromRoot(pHost);
                    if (pArena == nullptr) {
                        pArena = NebulaArena::current();
                    }
                    const NebulaArena::CurrentScope scope(pArena);
                    Part* const pPart = makePart(pHost, pArchive, follow, pJoint, rOffset, rTurn);
                    // The Low level, only where the host can draw a far model: a host in no view group (the player, an
                    // object placed without one or made during play) draws its base at every place, and so does its part.
                    const LodCtrl* const pLod = LodCtrlFunction::findOf(pHost);
                    if (pLod != nullptr && pLod->mFarModel != nullptr && LodCtrlFunction::isExistLevel(pArchive, "Low")) {
                        const MR::FormattedName low("%sLow", pArchive);
                        Part* const pLow = makePart(pHost, low.c_str(), follow, pJoint, rOffset, rTurn);
                        // Drawn while the host hides its own model for its far one.
                        pLow->actor->mIsShownWhileHostHidden = true;
                        pLow->actor->makeActorDead();
                        pPart->low = pLow;
                        pPart->hostLod = pLod;
                    }
                    pPart->next = sParts;
                    sParts = pPart;
                    return pPart;
                }

                Attached* attachedOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sAttached[i].handle == handle) {
                            return sAttached[i].mod == pMod ? &sAttached[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                // ---- Facts (harness only) ----

                void attachFact(const Attached& rAttached, const char* pName, u32 follow, const char* pJoint, bool placeholder) {
                    char mod[160];
                    char name[300];
                    char joint[160];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rAttached.mod));
                    luma::modlog::jsonString(name, sizeof(name), pName);
                    luma::modlog::jsonString(joint, sizeof(joint), pJoint != nullptr ? pJoint : "");
                    const Part* const pPart = rAttached.part;
                    char json[1024];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"mod\":%s,\"model\":%s,\"follow\":\"%s\",\"joint\":%s,\"placeholder\":%s,\"drawn\":%s,\"joints\":%u,"
                                    "\"mapped\":%u,\"reused\":%s,\"levels\":[%s]}",
                                    mod, name, followName(follow), joint, placeholder ? "true" : "false", pPart != nullptr ? "true" : "false",
                                    pPart != nullptr ? MR::getModelData(pPart->actor)->getJointCount() : 0u, [pPart]() {
                                        u32 mapped = 0;
                                        for (u32 i = 0; pPart != nullptr && pPart->map != nullptr && i < pPart->joints; i++) {
                                            mapped += pPart->map[pPart->order[i]] >= 0 ? 1u : 0u;
                                        }
                                        return mapped;
                                    }(),
                                    rAttached.reused ? "true" : "false", pPart != nullptr && pPart->low != nullptr ? "\"Low\"" : "");
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "model.attach.%llu", static_cast< unsigned long long >(rAttached.handle));
                    luma::modlog::fact(key, json);
                }

                // Where the model stands against what it follows (joint 0 of the model against the host's joint or base),
                // the largest distance between a skeleton's joints and the host's of their names, whether it is shown.
                void poseFact(const Attached& rAttached) {
                    const Part* const pPart = rAttached.part;
                    Model* const pModel = MR::getModel(pPart->actor);
                    const MtxPtr root = pModel->getAnmMtx(0);
                    Vec3f hostAt;
                    if (pPart->joint != nullptr) {
                        const MtxPtr m = MR::getJointMtx(pPart->host, pPart->joint);
                        hostAt.set(m[0][3], m[1][3], m[2][3]);
                    } else {
                        const MtxPtr m = pPart->host->getBaseMtx();
                        if (m != nullptr) {
                            hostAt.set(m[0][3], m[1][3], m[2][3]);
                        } else {
                            hostAt = pPart->host->mPosition;
                        }
                    }
                    f32 worst = 0.0f;
                    if (pPart->map != nullptr && MR::getModelData(pPart->host) == pPart->hostData) {
                        Model* const pHost = MR::getModel(pPart->host);
                        for (u32 k = 0; k < pPart->joints; k++) {
                            const u32 i = pPart->order[k];
                            if (pPart->map[i] < 0) {
                                continue;
                            }
                            const MtxPtr a = pModel->getAnmMtx(static_cast< int >(i));
                            const MtxPtr b = pHost->getAnmMtx(pPart->map[i]);
                            const f32 dx = a[0][3] - b[0][3];
                            const f32 dy = a[1][3] - b[1][3];
                            const f32 dz = a[2][3] - b[2][3];
                            const f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
                            worst = d > worst ? d : worst;
                        }
                    }
                    const f32 gx = root[0][3] - hostAt.x;
                    const f32 gy = root[1][3] - hostAt.y;
                    const f32 gz = root[2][3] - hostAt.z;
                    const f32 gap = sqrtf(gx * gx + gy * gy + gz * gz);
                    const bool shown = !pPart->actor->mIsDead && !MR::isDead(pPart->actor) && !MR::isHiddenModel(pPart->actor);
                    char json[352];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"frame\":%u,\"at\":[%.3f,%.3f,%.3f],\"host_at\":[%.3f,%.3f,%.3f],\"gap\":%.4f,\"worst\":%.4f,\"shown\":%s}",
                                    rAttached.frame, static_cast< double >(root[0][3]), static_cast< double >(root[1][3]),
                                    static_cast< double >(root[2][3]), static_cast< double >(hostAt.x), static_cast< double >(hostAt.y),
                                    static_cast< double >(hostAt.z), static_cast< double >(gap), static_cast< double >(worst), shown ? "true" : "false");
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "model.pose.%llu", static_cast< unsigned long long >(rAttached.handle));
                    luma::modlog::fact(key, json);
                }

                // The handle ends: why is "detach", "actor", "scene" or "mod". `stop`: the part stops drawing (not at a
                // scene's end, whose memory is going).
                void end(Attached& rAttached, const char* pWhy, bool stop) {
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "model.end.%llu", static_cast< unsigned long long >(rAttached.handle));
                        char json[48];
                        nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                        luma::modlog::fact(key, json);
                    }
                    if (Part* const pPart = rAttached.part; pPart != nullptr) {
                        if (stop && !MR::isDead(pPart->actor)) {
                            pPart->actor->makeActorDead();
                        }
                        if (stop && pPart->low != nullptr && !MR::isDead(pPart->low->actor)) {
                            pPart->low->actor->makeActorDead();
                        }
                        pPart->busy = false;
                    }
                    rAttached = Attached();
                    sLive--;
                    while (sHigh > 0 && sAttached[sHigh - 1].handle == 0) {
                        sHigh--;
                    }
                }

                // The host's level this frame: the Low part alive exactly while the host's far model is (LodCtrl::update makes
                // it appear for the view cube's far level), drawn when the host is in view as any part; the base part is
                // hidden with the host meanwhile (PartsModel::movement). A fact when it changes.
                void followLevel(Attached& rAttached, bool facts) {
                    const Part* const pPart = rAttached.part;
                    const ModelObj* const pFar = pPart->hostLod->mFarModel;
                    const bool far = !MR::isDead(pFar);
                    PartsModel* const pLow = pPart->low->actor;
                    if (far && MR::isDead(pLow)) {
                        pLow->makeActorAppeared();
                    } else if (!far && !MR::isDead(pLow)) {
                        pLow->makeActorDead();
                    }
                    const s8 level = far ? 1 : 0;
                    if (facts && level != rAttached.level) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "model.level.%llu", static_cast< unsigned long long >(rAttached.handle));
                        char json[64];
                        nebula_snprintf(json, sizeof(json), "{\"frame\":%u,\"level\":\"%s\"}", rAttached.frame, far ? "Low" : "base");
                        luma::modlog::fact(key, json);
                    }
                    rAttached.level = level;
                }

                // ---- The service ----

                int32_t svcAttach(const LumaMod* pMod, LumaActor actor, LumaModelName name, LumaContentModelFollow follow,
                                  const char* pJoint, const LumaPlace* pPlace, LumaContentModel* pOut) {
                    const char* const pWhere = "content.model.attach";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    LiveActor* const pHost = static_cast< LiveActor* >(actor);
                    const u32 followed = static_cast< u32 >(follow);
                    const char* pWrong = pHost == nullptr                                    ? "actor"
                                         : name == nullptr || name[0] == 0                   ? "model"
                                         : followed > LUMA_CONTENT_MODEL_SKELETON            ? "follow"
                                         : pOut == nullptr                                    ? "out"
                                                                                              : nullptr;
                    if (pWrong == nullptr && (!lumacore::isA(lumacore::classOf(pHost), lumacore::kClassId< LiveActor >) ||
                                              MR::isDead(pHost) || pHost->mModelManager == nullptr)) {
                        pWrong = "actor";
                    }
                    if (pWrong == nullptr && isLevelBlock(name)) {
                        pWrong = "model";
                    }
                    if (pWrong != nullptr) {
                        report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    if (pPlace != nullptr && pPlace->size < sizeof(LumaPlace)) {
                        char found[16];
                        char expected[16];
                        nebula_snprintf(found, sizeof(found), "%u", pPlace->size);
                        nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaPlace)));
                        report(pMod, pWhere, 103u, "LumaPlace", found, expected);
                        return LUMA_E_ARGUMENT;
                    }
                    u32 open = 0;
                    u32 slot = kMostModels;
                    for (u32 i = 0; i < kMostModels; i++) {
                        if (sAttached[i].handle == 0) {
                            if (slot == kMostModels) {
                                slot = i;
                            }
                            if (i >= sHigh) {
                                break;
                            }
                        } else if (sAttached[i].mod == pMod) {
                            open++;
                        }
                    }
                    if (open >= LUMA_CONTENT_MODEL_MOST || slot == kMostModels) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", open >= LUMA_CONTENT_MODEL_MOST ? LUMA_CONTENT_MODEL_MOST : kMostModels);
                        report(pMod, pWhere, 104u, text, nullptr, nullptr);
                        return LUMA_E_LIMIT;
                    }

                    // The model (references.md 4): a name in no store has the placeholder in its place.
                    const char* pArchive = name;
                    bool placeholder = false;
                    if (!MR::isExistObjectOrLayoutArchive(name)) {
                        sayContent(pMod, cMissingModelCode, "Content.MissingModel", MR::getHashCode(name), "found", name, nullptr, nullptr);
                        placeholder = true;
                        pArchive = MR::isExistObjectOrLayoutArchive(cPlaceholder) ? cPlaceholder : nullptr;
                        if (pArchive == nullptr) {
                            nebula_log("content.model: the placeholder model %s is not in the store; nothing shows for %s\n", cPlaceholder,
                                       name);
                        }
                    }

                    // The joint (Content.MissingJoint: the base).
                    const char* pFollowed = nullptr;
                    if (followed == LUMA_CONTENT_MODEL_JOINT && pJoint != nullptr && pJoint[0] != 0) {
                        if (MR::getModelData(pHost)->findJoint(pJoint) >= 0) {
                            pFollowed = pJoint;
                        } else {
                            char column[200];
                            nebula_snprintf(column, sizeof(column), "%s/%s", name, pJoint);
                            sayContent(pMod, cMissingJointCode, "Content.MissingJoint", MR::getHashCode(column), "model", modelNameOf(pHost),
                                       "found", pJoint);
                        }
                    }
                    Vec3f offset(0.0f, 0.0f, 0.0f);
                    Vec3f turn(0.0f, 0.0f, 0.0f);
                    if (pPlace != nullptr && followed != LUMA_CONTENT_MODEL_SKELETON) {
                        offset.set(pPlace->offset[0], pPlace->offset[1], pPlace->offset[2]);
                        turn.set(pPlace->turn[0], pPlace->turn[1], pPlace->turn[2]);
                    }

                    Attached& rAttached = sAttached[slot];
                    rAttached = Attached();
                    rAttached.handle = ++sNextHandle;
                    rAttached.mod = pMod;
                    rAttached.host = pHost;
                    rAttached.level = -1;
                    if (pArchive != nullptr) {
                        Part* const pPart = partFor(pHost, pArchive, followed, pFollowed, offset, turn, &rAttached.reused);
                        pPart->busy = true;
                        rAttached.part = pPart;
                    }
                    if (slot >= sHigh) {
                        sHigh = slot + 1;
                    }
                    sLive++;
                    if (luma::modlog::facts()) {
                        attachFact(rAttached, placeholder ? cPlaceholder : name, followed, pJoint, placeholder);
                    }
                    *pOut = rAttached.handle;
                    return LUMA_OK;
                }

                int32_t svcDetach(const LumaMod* pMod, LumaContentModel model) {
                    const char* const pWhere = "content.model.detach";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Attached* const pAttached = attachedOf(pMod, model);
                    if (pAttached == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    end(*pAttached, "detach", true);
                    return LUMA_OK;
                }

                int32_t svcShow(const LumaMod* pMod, LumaContentModel model, bool on) {
                    const char* const pWhere = "content.model.show";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Attached* const pAttached = attachedOf(pMod, model);
                    if (pAttached == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (Part* const pPart = pAttached->part; pPart != nullptr) {
                        for (Part* p = pPart; p != nullptr; p = p == pPart ? pPart->low : nullptr) {
                            if (on) {
                                MR::showModelIfHidden(p->actor);
                            } else {
                                MR::hideModelIfShown(p->actor);
                            }
                        }
                    }
                    return LUMA_OK;
                }

                int32_t svcPlayerParts(const LumaMod* pMod, LumaPlayerPart* pParts, uint32_t count, uint32_t* pTotal) {
                    const char* const pWhere = "content.model.player_parts";
                    if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                        sKernel->unknown_mod(pWhere);
                        return LUMA_E_ARGUMENT;
                    }
                    if (sKernel->mod_live(pMod) == 0) {
                        return LUMA_E_CANCELLED;
                    }
                    if (count != 0 && pParts == nullptr) {
                        report(pMod, pWhere, 101u, "parts", nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    for (u32 i = 0; i < count; i++) {
                        if (pParts[i].size < sizeof(LumaPlayerPart)) {
                            char found[16];
                            char expected[16];
                            nebula_snprintf(found, sizeof(found), "%u", pParts[i].size);
                            nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaPlayerPart)));
                            report(pMod, pWhere, 103u, "LumaPlayerPart", found, expected);
                            return LUMA_E_ARGUMENT;
                        }
                    }
                    for (u32 i = 0; i < count && i < cPlayerPartCount; i++) {
                        pParts[i].name = cPlayerParts[i].name;
                        pParts[i].joint = cPlayerParts[i].joint;
                    }
                    if (pTotal != nullptr) {
                        *pTotal = cPlayerPartCount;
                    }
                    return LUMA_OK;
                }

                // Content.MissingParam (references.md 6): the model, the parameter and its material.
                void sayMissingParam(const LumaMod* pMod, const char* pModel, const char* pMaterial, const char* pParam) {
                    char column[300];
                    nebula_snprintf(column, sizeof(column), "%s/%s/%s", pModel, pMaterial, pParam);
                    LumaModLogEntry entry = {};
                    entry.size = sizeof(entry);
                    entry.source = LUMA_MODLOG_CONTENT;
                    entry.level = LUMA_MODLOG_ERROR;
                    entry.code = cMissingParamCode;
                    entry.mod = sKernel->mod_id(pMod);
                    entry.file = "";
                    entry.column = MR::getHashCode(column);
                    entry.names[0] = "model";
                    entry.values[0] = pModel;
                    entry.names[1] = "found";
                    entry.values[1] = pParam;
                    entry.names[2] = "material";
                    entry.values[2] = pMaterial;
                    entry.count = 3;
                    (void)luma::modlog::add(entry, "Content.MissingParam");
                }

                // The place (0 to 6, the material's rows: NEBULA_ROW_REGISTER 0-2, NEBULA_ROW_CONSTANT 3-6) the cook's
                // <model>.params gives the material's parameter; -1: none (no file, not the cook's table, no row).
                s32 paramPlace(const ResourceHolder* pHolder, const char* pModel, const char* pMaterial, const char* pParam) {
                    char file[300];
                    nebula_snprintf(file, sizeof(file), "%s.params", pModel);
                    const void* const pFile = pHolder->findFile(file);
                    if (pFile == nullptr || memcmp(pFile, "NTBL", 4) != 0) {
                        return -1;
                    }
                    ::Table table;
                    table.attach(pFile);
                    const s32 materialColumn = table.searchItemInfo("material");
                    const s32 paramColumn = table.searchItemInfo("param");
                    const s32 placeColumn = table.searchItemInfo("register");
                    if (materialColumn < 0 || paramColumn < 0 || placeColumn < 0 || table.getColumnKind("material") != TableKind_Text ||
                        table.getColumnKind("param") != TableKind_Text || table.getColumnKind("register") != TableKind_Int) {
                        return -1;
                    }
                    const int rows = table.getNumEntries();
                    for (int r = 0; r < rows; r++) {
                        const char* pRowMaterial = nullptr;
                        const char* pRowParam = nullptr;
                        s32 place = -1;
                        if (table.getValueFast(r, materialColumn, &pRowMaterial) && table.getValueFast(r, paramColumn, &pRowParam) &&
                            strcmp(pRowMaterial, pMaterial) == 0 && strcmp(pRowParam, pParam) == 0 &&
                            table.getValueFast(r, placeColumn, &place)) {
                            return place >= 0 && place <= 6 ? place : -1;
                        }
                    }
                    return -1;
                }

                // The parameter written into the actor's own copy of the material (Model::ownMaterial: the same model file's
                // others keep theirs). The place written, or -1: the model `pName` has no such material or parameter.
                s32 writeParam(LiveActor* pActor, const char* pName, const char* pMaterial, const char* pParam, const float* pRgba) {
                    Model* const pModel = MR::getModel(pActor);
                    const s32 place = paramPlace(MR::getModelResourceHolder(pActor), pName, pMaterial, pParam);
                    const ModelData* const pData = pModel->getModelData();
                    u32 index = pData->getMaterialCount();
                    for (u32 i = 0; i < pData->getMaterialCount(); i++) {
                        if (strcmp(pData->getMaterialName(i), pMaterial) == 0) {
                            index = i;
                            break;
                        }
                    }
                    if (place < 0 || index == pData->getMaterialCount()) {
                        return -1;
                    }
                    Material* const pOwn = pModel->ownMaterial(index);
                    const auto held = [](f32 value, f32 low, f32 high) {
                        const f32 steps = std::round(value * 255.0f);
                        return steps < low ? low : steps > high ? high : steps;
                    };
                    if (place < 3) {
                        NebulaColor16* const pColor = pOwn->getRegisterColor(static_cast< u32 >(place));
                        pColor->r = static_cast< s16 >(held(pRgba[0], -1024.0f, 1023.0f));
                        pColor->g = static_cast< s16 >(held(pRgba[1], -1024.0f, 1023.0f));
                        pColor->b = static_cast< s16 >(held(pRgba[2], -1024.0f, 1023.0f));
                        pColor->a = static_cast< s16 >(held(pRgba[3], -1024.0f, 1023.0f));
                    } else {
                        NebulaColor8* const pColor = pOwn->getConstantColor(static_cast< u32 >(place - 3));
                        pColor->r = static_cast< u8 >(held(pRgba[0], 0.0f, 255.0f));
                        pColor->g = static_cast< u8 >(held(pRgba[1], 0.0f, 255.0f));
                        pColor->b = static_cast< u8 >(held(pRgba[2], 0.0f, 255.0f));
                        pColor->a = static_cast< u8 >(held(pRgba[3], 0.0f, 255.0f));
                    }
                    return place;
                }

                int32_t svcSetParam(const LumaMod* pMod, LumaContentModel model, const char* pMaterial, const char* pParam,
                                    const float* pRgba) {
                    const char* const pWhere = "content.model.set_param";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Attached* const pAttached = attachedOf(pMod, model);
                    if (pAttached == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    const char* pWrong = pMaterial == nullptr ? "material" : pParam == nullptr ? "param" : pRgba == nullptr ? "rgba" : nullptr;
                    for (u32 i = 0; pWrong == nullptr && i < 4; i++) {
                        if (!std::isfinite(pRgba[i])) {
                            pWrong = "rgba";
                        }
                    }
                    if (pWrong != nullptr) {
                        report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    // The part's model and its archive (a model that shows nothing has neither).
                    LiveActor* const pActor = pAttached->part != nullptr ? pAttached->part->actor : nullptr;
                    const ResourceHolder* const pHolder = pActor != nullptr ? MR::getModelResourceHolder(pActor) : nullptr;
                    Model* const pModel = pActor != nullptr ? MR::getModel(pActor) : nullptr;
                    const char* const pName = pHolder != nullptr ? pHolder->getModelName() : nullptr;
                    if (pModel == nullptr || pName == nullptr) {
                        sayMissingParam(pMod, "-", pMaterial, pParam);
                        return LUMA_E_NOT_FOUND;
                    }
                    const s32 place = writeParam(pActor, pName, pMaterial, pParam, pRgba);
                    if (place < 0) {
                        sayMissingParam(pMod, pName, pMaterial, pParam);
                        return LUMA_E_NOT_FOUND;
                    }
                    // The Low level takes the same value where its own model has that material and parameter.
                    if (const Part* const pLow = pAttached->part->low; pLow != nullptr) {
                        const ResourceHolder* const pLowHolder = MR::getModelResourceHolder(pLow->actor);
                        if (pLowHolder != nullptr && pLowHolder->getModelName() != nullptr) {
                            (void)writeParam(pLow->actor, pLowHolder->getModelName(), pMaterial, pParam, pRgba);
                        }
                    }
                    if (luma::modlog::facts()) {
                        char material[160];
                        char param[160];
                        luma::modlog::jsonString(material, sizeof(material), pMaterial);
                        luma::modlog::jsonString(param, sizeof(param), pParam);
                        char json[512];
                        nebula_snprintf(json, sizeof(json),
                                        "{\"material\":%s,\"param\":%s,\"register\":%d,\"rgba\":[%.4f,%.4f,%.4f,%.4f]}",
                                        material, param, place, static_cast< double >(pRgba[0]), static_cast< double >(pRgba[1]),
                                        static_cast< double >(pRgba[2]), static_cast< double >(pRgba[3]));
                        char key[64];
                        nebula_snprintf(key, sizeof(key), "content.model.param.%llu",
                                        static_cast< unsigned long long >(pAttached->handle));
                        luma::modlog::fact(key, json);
                    }
                    return LUMA_OK;
                }

                const LumaContentModelService cTable = {
                    {sizeof(LumaContentModelService), LUMA_CONTENT_MODEL_MAJOR, LUMA_CONTENT_MODEL_MINOR},
                    svcAttach,
                    svcDetach,
                    svcShow,
                    svcPlayerParts,
                    svcSetParam,
                };

                // ---- The provider ----

                // The mod's stop (or its taking out): its models stop drawing.
                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sAttached[i].handle != 0 && sAttached[i].mod == pMod) {
                            end(sAttached[i], "mod", true);
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_CONTENT_MODEL_NAME,
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
                const bool facts = luma::modlog::facts();
                for (u32 i = 0; i < sHigh; i++) {
                    Attached& rAttached = sAttached[i];
                    if (rAttached.handle == 0) {
                        continue;
                    }
                    if (MR::isDead(rAttached.host)) {
                        end(rAttached, "actor", true);
                        continue;
                    }
                    if (rAttached.part != nullptr && rAttached.part->low != nullptr) {
                        followLevel(rAttached, facts);
                    }
                    if (facts && rAttached.part != nullptr && rAttached.frame < static_cast< u32 >(120)) {
                        poseFact(rAttached);
                    }
                    rAttached.frame++;
                }
            }

            void released(const void* pStart, const void* pEnd) {
                if (sParts == nullptr && sLive == 0) {
                    return;
                }
                for (u32 i = 0; i < sHigh; i++) {
                    Attached& rAttached = sAttached[i];
                    if (rAttached.handle != 0 && (inRange(rAttached.host, pStart, pEnd) ||
                                                  (rAttached.part != nullptr && inRange(rAttached.part, pStart, pEnd)))) {
                        end(rAttached, "scene", false);
                    }
                }
                Part** ppLink = &sParts;
                while (*ppLink != nullptr) {
                    Part* const pPart = *ppLink;
                    if (inRange(pPart, pStart, pEnd) || inRange(pPart->host, pStart, pEnd)) {
                        *ppLink = pPart->next;
                    } else {
                        ppLink = &pPart->next;
                    }
                }
            }

            LiveActor* actorOf(const LumaMod* pMod, LumaHandle handle) {
                if (sLive == 0) {
                    return nullptr;
                }
                const Attached* const pAttached = attachedOf(pMod, handle);
                if (pAttached == nullptr) {
                    return nullptr;
                }
                return pAttached->part != nullptr ? static_cast< LiveActor* >(pAttached->part->actor) : pAttached->host;
            }

            LiveActor* partOf(const LumaMod* pMod, LumaHandle handle, bool* pOpen) {
                const Attached* const pAttached = sLive == 0 ? nullptr : attachedOf(pMod, handle);
                *pOpen = pAttached != nullptr;
                return pAttached != nullptr && pAttached->part != nullptr ? static_cast< LiveActor* >(pAttached->part->actor)
                                                                          : nullptr;
            }

            const LumaServiceProvider* provider() { return &cProvider; }
        }
    }
}
