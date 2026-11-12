#include "Game/System/LumaContentFx.hpp"

#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/Effect/SingleEmitter.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/LumaContentModel.hpp"
#include <luma.h>
#include <luma_content_fx.h>
#include <luma_modlog.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_math.h>
#include <nebula_module.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

// The content.fx service (contract effects). Game thread throughout: the service's calls, the frame, the provider.
// NebulaArena::destroy is the scene end's (Arenas), on the game thread as well.
//
// An effect is the game's own MultiEmitter with an AutoEffectInfo of its own, as a table's effect has one, so the game's
// callback follows it (MultiEmitterCallBack: the FOLLOW_* flags while `follow`, AFFECT_* at its making) and draws it in
// its group. It is not put in the actor's EffectKeeper (its size is fixed when the actor is made); the frame does for it
// what the keeper does for the actor's own: its end when the actor dies (EffectKeeper::clear) and the clip stop
// (stopEmitterOnClipped / playEmitterOffClipped).
//
// Memory: an effect's emitter, its info and its matrices (a Shell) are made in the actor's arena, so they go with the
// actor; a shell whose effect ended is used again for the same actor, name, place kind and follow once its particles are
// gone, so a mod starting one effect every frame keeps one shell per effect alive at a time. NebulaArena::destroy tells
// released() first, and nothing in that range is touched after.
//
// No cost with no mod (effects 5): frame() and released() are one load each while no effect was started.

NEBULA_TUNABLE_NAMED(s32, cFrames_check_1, "check.1", 60);  // EffectEmpty's window after an emitter's first frame (effects 2)
NEBULA_TUNABLE_NAMED(s32, cFrames_alive_1, "alive.1", 120); // fx.alive facts a handle writes, from its start (harness only)

namespace luma {
    namespace content {
        namespace fx {
            namespace {
                const u32 kMostEffects = LUMA_CONTENT_FX_MOST * 16u;  // across mods
                // The placeholder effect (references 4): Nebula's data/placeholder/effect.json, cooked into the store.
                const char cPlaceholder[] = "nebula:placeholder/effect";
                const u32 cMissingEffectCode = 623;
                const u32 cMissingJointCode = 626;
                const u32 cEffectEmptyCode = 640;

                struct Shell {
                    Shell* next;       // every shell, for reuse and release
                    LiveActor* actor;
                    MultiEmitter* emitter;
                    char* name;        // the effect's name (the arena's copy: the info and the emitter point at it)
                    MtxPtr source;     // the joint's or the base's matrix; null: the actor's position and rotation
                    const Vec3f* hostScale;  // the actor's scale for an effect at its base (as the game's table)
                    bool composed;     // the emitter follows `own` (source x turn) rather than `source`
                    bool follow;
                    bool busy;         // an open handle uses it
                    AutoEffectInfo info;
                    Mtx turn;
                    Mtx own;
                };

                struct Effect {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    LiveActor* actor;
                    Shell* shell;       // null: no particle of the name or the placeholder's exists: nothing shows
                    const char* name;   // the shell's name, or the asked one for an empty effect (the arena's or static)
                    u32 frame;          // frames the frame has seen it
                    bool clipped;
                    bool checkEmpty;    // section 2's window is open
                    LumaHandle model;   // the mod's model it was started on (start_on_model); 0: on an actor
                };

                const LumaServiceKernel* sKernel = nullptr;
                Effect sEffects[kMostEffects];
                u32 sHigh = 0;            // slots below are in use or were
                u32 sLive = 0;            // open handles
                LumaHandle sNextHandle = 0;
                Shell* sShells = nullptr;
                const void* sSystem = nullptr;  // the effect system the shells' emitters belong to

                // Effect names checked by section 2 this session (the first start of each), ::malloc, kept to the end.
                struct Checked {
                    Checked* next;
                    u32 hash;
                    char name[1];
                };
                Checked* sChecked = nullptr;

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

                // A CONTENT entry of the mod (effects 2; references 6). The column keeps two names two entries.
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

                // ---- Names ----

                // Every particle the name resolves to exists, by the game's rule (effects 7a; MultiEmitter::allocateEmitter):
                // a numbered name has at least its <name>00; a list has each word.
                bool resolves(const char* pName) {
                    u16 index = 0;
                    if (!MR::hasStringSpace(pName) && !MR::isDigitStringTail(pName, 2)) {
                        return MR::Effect::isExistInResource(&index, pName, 0);
                    }
                    const u32 length = static_cast< u32 >(strlen(pName));
                    u32 start = 0;
                    for (u32 i = 0; i <= length; i++) {
                        if (pName[i] == ' ' || pName[i] == '\0') {
                            if (i == start) {
                                return false;  // two spaces together, or one at an end: a word with no name
                            }
                            const MR::FormattedName word("%.*s", static_cast< int >(i - start), pName + start);
                            if (!MR::Effect::isExistInResource(&index, word.c_str())) {
                                return false;
                            }
                            start = i + 1;
                        }
                    }
                    return true;
                }

                // ---- Draw groups (effects 4; D5-5) ----

                // A mod's effects list as the cook writes it (G, effects "G on draft 4"): Luma/<id>/effects.txt, one
                // line an effect sorted by name, `<name>\t<group>\t<emitters>\n`. Read once a session per mod, ::malloc,
                // kept to the end (a few bytes an effect).
                struct GroupRow {
                    u32 hash;       // MR::getHashCode of the name
                    s32 group;      // 0 to 5
                    char name[64];  // the effect's own name (the cook's names are shorter)
                };
                struct ModGroups {
                    ModGroups* next;
                    char id[68];
                    u32 count;  // rows; 0 also when the list is absent or damaged (every effect then draws in group 0)
                    GroupRow rows[1];
                };
                ModGroups* sGroups = nullptr;
                const u32 cEffectsFileCode = 641;
                const u32 kMostEffectsFile = 64u * 1024u;

                bool nameChar(char c, bool first) {
                    return (c >= 'a' && c <= 'z') || (!first && ((c >= '0' && c <= '9') || c == '_'));
                }

                // The list's line `line` (from 1) outside the form: one Content.EffectsFile entry for the mod (its
                // repeats are counted by the log), and every effect of the mod in group 0.
                void sayEffectsFile(const char* pId, u32 line) {
                    char path[96];
                    char number[16];
                    nebula_snprintf(path, sizeof(path), "Luma/%s/effects.txt", pId);
                    nebula_snprintf(number, sizeof(number), "%u", line);
                    LumaModLogEntry entry = {};
                    entry.size = sizeof(entry);
                    entry.source = LUMA_MODLOG_CONTENT;
                    entry.level = LUMA_MODLOG_ERROR;
                    entry.code = cEffectsFileCode;
                    entry.mod = pId;
                    entry.file = path;
                    entry.line = line;
                    entry.names[0] = "line";
                    entry.values[0] = number;
                    entry.count = 1;
                    (void)luma::modlog::add(entry, "Content.EffectsFile");
                }

                // Parses `pText` (`length` bytes) into `pGroups`; false at the first line outside the form (*pBad its
                // number).
                bool parseGroups(const char* pText, u32 length, ModGroups* pGroups, u32* pBad) {
                    u32 at = 0;
                    u32 line = 0;
                    const char* pLast = nullptr;
                    u32 lastLength = 0;
                    while (at < length) {
                        line++;
                        *pBad = line;
                        const char* const pName = pText + at;
                        u32 n = 0;
                        while (at + n < length && nameChar(pText[at + n], n == 0)) {
                            n++;
                        }
                        if (n == 0 || at + n >= length || pText[at + n] != '\t') {
                            return false;
                        }
                        // Sorted by name in byte order, each once.
                        if (pLast != nullptr) {
                            const u32 common = n < lastLength ? n : lastLength;
                            const s32 order = memcmp(pLast, pName, common);
                            if (order > 0 || (order == 0 && lastLength >= n)) {
                                return false;
                            }
                        }
                        u32 p = at + n + 1;
                        if (p + 1 >= length || pText[p] < '0' || pText[p] > '5' || pText[p + 1] != '\t') {
                            return false;
                        }
                        const s32 group = pText[p] - '0';
                        p += 2;
                        u32 emitters = 0;
                        u32 digits = 0;
                        while (p < length && pText[p] >= '0' && pText[p] <= '9' && digits < 3) {
                            emitters = emitters * 10u + static_cast< u32 >(pText[p] - '0');
                            p++;
                            digits++;
                        }
                        if (digits == 0 || (digits > 1 && pText[p - digits] == '0') || emitters < 1u || emitters > 16u ||
                            p >= length || pText[p] != '\n') {
                            return false;
                        }
                        char name[64];
                        if (n >= sizeof(name)) {
                            return false;
                        }
                        memcpy(name, pName, n);
                        name[n] = 0;
                        memcpy(pGroups->rows[pGroups->count].name, name, n + 1);
                        pGroups->rows[pGroups->count].hash = MR::getHashCode(name);
                        pGroups->rows[pGroups->count].group = group;
                        pGroups->count++;
                        pLast = pName;
                        lastLength = n;
                        at = p + 1;
                    }
                    return true;
                }

                // The mod's list, read the first time one of its effects is made.
                const ModGroups* groupsOf(const char* pId, u32 idLength) {
                    for (const ModGroups* p = sGroups; p != nullptr; p = p->next) {
                        if (strlen(p->id) == idLength && memcmp(p->id, pId, idLength) == 0) {
                            return p;
                        }
                    }
                    if (idLength == 0 || idLength >= sizeof(ModGroups::id)) {
                        return nullptr;
                    }
                    char id[sizeof(ModGroups::id)];
                    memcpy(id, pId, idLength);
                    id[idLength] = 0;
                    char path[96];
                    nebula_snprintf(path, sizeof(path), "/Luma/%s/effects.txt", id);
                    GameAssets::File file;
                    const u32 length = GameAssets::open(path, &file) ? file.mLength : 0u;
                    // At most one row a line, and a line is at least 6 bytes ("a\t0\t1\n").
                    const u32 most = length <= kMostEffectsFile ? length / 6u + 1u : 1u;
                    ModGroups* const pGroups = static_cast< ModGroups* >(::malloc(sizeof(ModGroups) + most * sizeof(GroupRow)));
                    if (pGroups == nullptr) {
                        return nullptr;
                    }
                    memcpy(pGroups->id, id, idLength + 1);
                    pGroups->count = 0;
                    if (length > kMostEffectsFile) {
                        sayEffectsFile(id, 1);
                    } else if (length > 0) {
                        char* const pText = static_cast< char* >(::malloc(length));
                        u32 bad = 0;
                        if (pText != nullptr && GameAssets::read(&file, pText, static_cast< s32 >(length), 0) >= 0 &&
                            !parseGroups(pText, length, pGroups, &bad)) {
                            pGroups->count = 0;
                            sayEffectsFile(id, bad);
                        }
                        ::free(pText);
                    }
                    pGroups->next = sGroups;
                    sGroups = pGroups;
                    return pGroups;
                }

                // The draw group (effects 4) of an effect: a mod's (`<id>:<name>`) from its list, the game's own 0 (3D,
                // the game's default for one with no AutoEffectList row).
                s32 drawGroupOf(const char* pName) {
                    const char* const pColon = strchr(pName, ':');
                    if (pColon == nullptr) {
                        return 0;
                    }
                    const ModGroups* const pGroups = groupsOf(pName, static_cast< u32 >(pColon - pName));
                    if (pGroups == nullptr || pGroups->count == 0) {
                        return 0;
                    }
                    const u32 hash = MR::getHashCode(pColon + 1);
                    for (u32 i = 0; i < pGroups->count; i++) {
                        if (pGroups->rows[i].hash == hash && strcmp(pGroups->rows[i].name, pColon + 1) == 0) {
                            return pGroups->rows[i].group;
                        }
                    }
                    return 0;
                }

                bool firstCheck(const char* pName) {
                    const u32 hash = MR::getHashCode(pName);
                    for (const Checked* p = sChecked; p != nullptr; p = p->next) {
                        if (p->hash == hash && strcmp(p->name, pName) == 0) {
                            return false;
                        }
                    }
                    const size_t length = strlen(pName);
                    Checked* const pNew = static_cast< Checked* >(::malloc(sizeof(Checked) + length));
                    if (pNew == nullptr) {
                        NEBULA_PANIC("content.fx: no memory to note the effect %s", pName);
                    }
                    pNew->hash = hash;
                    memcpy(pNew->name, pName, length + 1);
                    pNew->next = sChecked;
                    sChecked = pNew;
                    return true;
                }

                const char* modelNameOf(const LiveActor* pActor) {
                    if (pActor->mModelManager != nullptr) {
                        if (const ResourceHolder* pHolder = MR::getModelResourceHolder(pActor); pHolder != nullptr) {
                            if (const char* pModel = pHolder->getModelName(); pModel != nullptr) {
                                return pModel;
                            }
                        }
                    }
                    return lumacore::classRow(lumacore::classOf(pActor)).name;
                }

                // ---- Places ----

                // `own` = (the joint's matrix, or the actor's position and rotation) x the turn.
                void compose(Shell* pShell) {
                    Mtx base;
                    if (pShell->source != nullptr) {
                        NebulaMtxCopy(pShell->source, base);
                    } else {
                        MR::makeMtxTR(base, pShell->actor);
                    }
                    NebulaMtxConcat(base, pShell->turn, pShell->own);
                }

                // The offset in the joint's space as the callback, which turns it by what it follows, needs it: under a
                // turn, the turn's inverse (its transpose) first, so the turn moves the effect's emitters, not its place.
                Vec3f offsetFor(const Shell* pShell, const Vec3f& rOffset) {
                    if (!pShell->composed) {
                        return rOffset;
                    }
                    const Mtx& r = pShell->turn;
                    return Vec3f(r[0][0] * rOffset.x + r[1][0] * rOffset.y + r[2][0] * rOffset.z,
                                 r[0][1] * rOffset.x + r[1][1] * rOffset.y + r[2][1] * rOffset.z,
                                 r[0][2] * rOffset.x + r[1][2] * rOffset.y + r[2][2] * rOffset.z);
                }

                Vec3f hostScaleOf(const Shell* pShell) {
                    if (pShell->hostScale != nullptr) {
                        return *pShell->hostScale;
                    }
                    const MtxPtr m = pShell->composed ? const_cast< Shell* >(pShell)->own : pShell->source;
                    return Vec3f(sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]),
                                 sqrtf(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]),
                                 sqrtf(m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]));
                }

                bool inRange(const void* p, const void* pStart, const void* pEnd) {
                    const uintptr_t at = reinterpret_cast< uintptr_t >(p);
                    return at >= reinterpret_cast< uintptr_t >(pStart) && at < reinterpret_cast< uintptr_t >(pEnd);
                }

                void setInfo(Shell* pShell, const Vec3f& rOffset, s32 group) {
                    AutoEffectInfo& rInfo = pShell->info;
                    rInfo.mGroupName = nullptr;
                    rInfo.mAnimName = nullptr;
                    rInfo.mUniqueName = pShell->name;
                    rInfo.mEffectName = pShell->name;
                    rInfo.mParentName = nullptr;
                    rInfo.mJointName = nullptr;
                    u16 flags = AutoEffectInfo::FLAG_AFFECT_TRANS | AutoEffectInfo::FLAG_AFFECT_ROTATE | AutoEffectInfo::FLAG_AFFECT_SCALE;
                    if (pShell->follow) {
                        flags |= AutoEffectInfo::FLAG_FOLLOW_TRANS | AutoEffectInfo::FLAG_FOLLOW_ROTATE | AutoEffectInfo::FLAG_FOLLOW_SCALE;
                    }
                    rInfo.mFlag = flags;
                    rInfo.mIsValidPrmColor = false;
                    rInfo.mIsValidEnvColor = false;
                    rInfo.mOffset = rOffset;
                    rInfo.mStartFrame = 0;
                    rInfo.mEndFrame = -1;
                    rInfo.mScaleValue = 1.0f;
                    rInfo.mRateValue = 1.0f;
                    rInfo.mLightAffectValue = 0.0f;
                    rInfo.mDrawOrder = group;
                }

                // A free shell for this actor, name, place kind and follow whose particles are all gone, or a new one.
                Shell* shellFor(LiveActor* pActor, const char* pName, MtxPtr pSource, const Vec3f* pHostScale, bool composed,
                                bool follow) {
                    for (Shell* p = sShells; p != nullptr; p = p->next) {
                        if (!p->busy && p->actor == pActor && p->source == pSource && p->composed == composed &&
                            p->follow == follow && strcmp(p->name, pName) == 0 && !p->emitter->isValid()) {
                            return p;
                        }
                    }
                    NebulaArena* pArena = NebulaArena::findFromRoot(pActor);
                    if (pArena == nullptr) {
                        pArena = NebulaArena::current();
                    }
                    const NebulaArena::CurrentScope scope(pArena);
                    Shell* const pShell = new Shell();
                    const size_t length = strlen(pName);
                    pShell->name = new char[length + 1];
                    memcpy(pShell->name, pName, length + 1);
                    pShell->actor = pActor;
                    pShell->source = pSource;
                    pShell->hostScale = pHostScale;
                    pShell->composed = composed;
                    pShell->follow = follow;
                    pShell->busy = false;
                    NebulaMtxIdentity(pShell->turn);
                    NebulaMtxIdentity(pShell->own);
                    const Vec3f zero(0.0f, 0.0f, 0.0f);
                    MtxPtr pFollowed = composed ? pShell->own : pSource;
                    pShell->emitter = pHostScale != nullptr ? new MultiEmitter(pShell->name, pFollowed, pHostScale, zero)
                                                            : new MultiEmitter(pShell->name, pFollowed, zero);
                    setInfo(pShell, zero, drawGroupOf(pName));
                    pShell->emitter->mAutoInfo = &pShell->info;
                    pShell->emitter->setDrawOrder(pShell->info.mDrawOrder);
                    pShell->emitter->scanParticleEmitter(MR::getEffectSystem());
                    pShell->next = sShells;
                    sShells = pShell;
                    sSystem = MR::getEffectSystem();
                    return pShell;
                }

                Effect* effectOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < sHigh; i++) {
                        if (sEffects[i].handle == handle) {
                            return sEffects[i].mod == pMod ? &sEffects[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                // ---- Facts (harness only) ----

                void startFact(const Effect& rEffect, const char* pJoint, bool follow, bool placeholder) {
                    char mod[160];
                    char name[300];
                    char joint[160];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rEffect.mod));
                    luma::modlog::jsonString(name, sizeof(name), rEffect.name);
                    luma::modlog::jsonString(joint, sizeof(joint), pJoint != nullptr ? pJoint : "");
                    char particles[200];
                    u32 at = 0;
                    particles[at++] = '[';
                    s32 group = 0;
                    s32 emitters = 0;
                    if (rEffect.shell != nullptr) {
                        group = rEffect.shell->info.mDrawOrder;
                        const MultiEmitter* const pEmitter = rEffect.shell->emitter;
                        emitters = pEmitter->mEmitters.size();
                        for (s32 i = 0; i < pEmitter->mEmitters.size() && at + 8 < sizeof(particles); i++) {
                            at += static_cast< u32 >(nebula_snprintf(particles + at, sizeof(particles) - at, i == 0 ? "%u" : ",%u",
                                                                     static_cast< u32 >(pEmitter->mEmitters[i].mResourceId)));
                        }
                    }
                    particles[at++] = ']';
                    particles[at] = 0;
                    char json[1024];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"mod\":%s,\"name\":%s,\"particles\":%s,\"emitters\":%d,\"group\":%d,\"joint\":%s,\"follow\":%s,"
                                    "\"placeholder\":%s,\"model\":%llu}",
                                    mod, name, particles, static_cast< int >(emitters), group, joint, follow ? "true" : "false", placeholder ? "true" : "false",
                                    static_cast< unsigned long long >(rEffect.model));
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "fx.start.%llu", static_cast< unsigned long long >(rEffect.handle));
                    luma::modlog::fact(key, json);
                }

                void aliveFact(const Effect& rEffect) {
                    const Shell* const pShell = rEffect.shell;
                    u32 particles = 0;
                    Vec3f at(0.0f, 0.0f, 0.0f);
                    bool found = false;
                    for (s32 i = 0; i < pShell->emitter->mEmitters.size(); i++) {
                        const SingleEmitter& rSingle = pShell->emitter->mEmitters[i];
                        if (rSingle.isValid()) {
                            const EmitterCore* const pCore = rSingle.mEmitter->mEmitter;
                            particles += pCore->getParticleNumber();
                            if (!found) {
                                at = pCore->mGlobalTrs;
                                found = true;
                            }
                        }
                    }
                    Vec3f jointAt;
                    if (pShell->source != nullptr) {
                        jointAt.set(pShell->source[0][3], pShell->source[1][3], pShell->source[2][3]);
                    } else {
                        jointAt = pShell->actor->mPosition;
                    }
                    char json[320];
                    nebula_snprintf(json, sizeof(json),
                                    "{\"frame\":%u,\"particles\":%u,\"at\":[%.3f,%.3f,%.3f],\"joint_at\":[%.3f,%.3f,%.3f]}",
                                    rEffect.frame, particles, static_cast< double >(at.x), static_cast< double >(at.y),
                                    static_cast< double >(at.z), static_cast< double >(jointAt.x), static_cast< double >(jointAt.y),
                                    static_cast< double >(jointAt.z));
                    char key[48];
                    nebula_snprintf(key, sizeof(key), "fx.alive.%llu", static_cast< unsigned long long >(rEffect.handle));
                    luma::modlog::fact(key, json);
                }

                // The handle ends (effects 3): why is "stop", "actor", "model", "scene" or "mod". The caller has already
                // stopped the emitter as the reason says (or must not touch it: "scene").
                void end(Effect& rEffect, const char* pWhy) {
                    if (luma::modlog::facts()) {
                        char key[48];
                        nebula_snprintf(key, sizeof(key), "fx.end.%llu", static_cast< unsigned long long >(rEffect.handle));
                        char json[48];
                        nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\"}", pWhy);
                        luma::modlog::fact(key, json);
                    }
                    if (rEffect.shell != nullptr) {
                        rEffect.shell->busy = false;
                    }
                    rEffect = Effect();
                    sLive--;
                    while (sHigh > 0 && sEffects[sHigh - 1].handle == 0) {
                        sHigh--;
                    }
                }

                // Section 2: no particle alive on any of its emitters for the window after each emitter's first frame
                // (EmitterCore::getAge counts from it: ParticleResource's calc waits out the start frame first), or
                // every emitter gone without one. Clipped, an emitter does not age (StopCalc).
                void check(Effect& rEffect) {
                    const MultiEmitter* const pEmitter = rEffect.shell->emitter;
                    u32 particles = 0;
                    bool anyValid = false;
                    bool allPast = true;
                    for (s32 i = 0; i < pEmitter->mEmitters.size(); i++) {
                        const SingleEmitter& rSingle = pEmitter->mEmitters[i];
                        if (rSingle.isValid()) {
                            anyValid = true;
                            const EmitterCore* const pCore = rSingle.mEmitter->mEmitter;
                            particles += pCore->getParticleNumber();
                            if (pCore->getAge() < static_cast< u32 >(cFrames_check_1)) {
                                allPast = false;
                            }
                        }
                    }
                    if (particles != 0) {
                        rEffect.checkEmpty = false;
                        return;
                    }
                    if (anyValid && !allPast) {
                        return;
                    }
                    rEffect.checkEmpty = false;
                    sayContent(rEffect.mod, cEffectEmptyCode, "Content.EffectEmpty", MR::getHashCode(rEffect.name), "found", rEffect.name,
                               nullptr, nullptr);
                }

                // ---- The service ----

                // start and start_on_model: `model` the mod's model handle the actor draws (0: an actor of the game's).
                int32_t startOn(const LumaMod* pMod, const char* pWhere, LumaActor actor, LumaEffectName name, const char* pJoint,
                                const LumaPlace* pPlace, bool follow, LumaContentFxEffect* pOut, LumaHandle model) {
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    LiveActor* const pActor = static_cast< LiveActor* >(actor);
                    const char* pWrong = pActor == nullptr                                            ? "actor"
                                         : name == nullptr || name[0] == 0                           ? "name"
                                         : pOut == nullptr                                            ? "out"
                                                                                                      : nullptr;
                    if (pWrong == nullptr &&
                        (!lumacore::isA(lumacore::classOf(pActor), lumacore::kClassId< LiveActor >) || MR::isDead(pActor))) {
                        pWrong = "actor";
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
                    u32 slot = kMostEffects;
                    for (u32 i = 0; i < kMostEffects; i++) {
                        if (sEffects[i].handle == 0) {
                            if (slot == kMostEffects) {
                                slot = i;
                            }
                            if (i >= sHigh) {
                                break;
                            }
                        } else if (sEffects[i].mod == pMod) {
                            open++;
                        }
                    }
                    if (open >= LUMA_CONTENT_FX_MOST || slot == kMostEffects) {
                        char text[16];
                        nebula_snprintf(text, sizeof(text), "%u", open >= LUMA_CONTENT_FX_MOST ? LUMA_CONTENT_FX_MOST : kMostEffects);
                        report(pMod, pWhere, 104u, text, nullptr, nullptr);
                        return LUMA_E_LIMIT;
                    }

                    // The name (section 2): a built name in no store has the placeholder in its place.
                    const char* pName = name;
                    bool placeholder = false;
                    if (!resolves(name)) {
                        sayContent(pMod, cMissingEffectCode, "Content.MissingEffect", MR::getHashCode(name), "found", name, nullptr,
                                   nullptr);
                        placeholder = true;
                        pName = resolves(cPlaceholder) ? cPlaceholder : nullptr;
                        if (pName == nullptr) {
                            nebula_log("content.fx: the placeholder effect %s is not in the store; nothing shows for %s\n", cPlaceholder,
                                       name);
                        }
                    }

                    // The place: the joint (Content.MissingJoint: the base), the base's matrix, or the actor's position.
                    MtxPtr pSource = nullptr;
                    const Vec3f* pHostScale = nullptr;
                    if (pJoint != nullptr && pJoint[0] != 0) {
                        const s32 index = pActor->mModelManager != nullptr ? MR::getModelData(pActor)->findJoint(pJoint) : -1;
                        if (index >= 0) {
                            pSource = MR::getJointMtx(pActor, index);
                        } else {
                            char column[200];
                            nebula_snprintf(column, sizeof(column), "%s/%s", name, pJoint);
                            sayContent(pMod, cMissingJointCode, "Content.MissingJoint", MR::getHashCode(column), "model",
                                       modelNameOf(pActor), "found", pJoint);
                        }
                    }
                    if (pSource == nullptr) {
                        pSource = pActor->getBaseMtx();
                        pHostScale = &pActor->mScale;
                    }
                    Vec3f offset(0.0f, 0.0f, 0.0f);
                    Vec3f turn(0.0f, 0.0f, 0.0f);
                    if (pPlace != nullptr) {
                        offset.set(pPlace->offset[0], pPlace->offset[1], pPlace->offset[2]);
                        turn.set(pPlace->turn[0], pPlace->turn[1], pPlace->turn[2]);
                    }
                    const bool turned = turn.x != 0.0f || turn.y != 0.0f || turn.z != 0.0f;
                    const bool composed = turned || pSource == nullptr;

                    Effect& rEffect = sEffects[slot];
                    rEffect = Effect();
                    rEffect.handle = ++sNextHandle;
                    rEffect.mod = pMod;
                    rEffect.actor = pActor;
                    rEffect.model = model;
                    rEffect.name = pName != nullptr ? pName : cPlaceholder;
                    if (pName != nullptr) {
                        Shell* const pShell = shellFor(pActor, pName, pSource, pHostScale, composed, follow);
                        pShell->busy = true;
                        if (composed) {
                            MR::makeMtxRotate(pShell->turn, turn.x, turn.y, turn.z);
                            compose(pShell);
                        }
                        const Vec3f place = offsetFor(pShell, offset);
                        pShell->info.mOffset = place;
                        pShell->emitter->setOffset(place);
                        // A shell used again starts as a new one: no tint of its last effect.
                        pShell->emitter->setBaseScale(1.0f);
                        pShell->emitter->setGlobalPrmColor(255, 255, 255, -1);
                        pShell->emitter->createEmitter();
                        rEffect.shell = pShell;
                        rEffect.name = pShell->name;
                        if (MR::isClipped(pActor)) {
                            pShell->emitter->stopEmitterOnClipped();
                            rEffect.clipped = true;
                        }
                        rEffect.checkEmpty = !placeholder && firstCheck(pShell->name);
                    }
                    if (slot >= sHigh) {
                        sHigh = slot + 1;
                    }
                    sLive++;
                    if (luma::modlog::facts()) {
                        startFact(rEffect, pJoint, follow, placeholder);
                    }
                    *pOut = rEffect.handle;
                    return LUMA_OK;
                }

                int32_t svcStart(const LumaMod* pMod, LumaActor actor, LumaEffectName name, const char* pJoint, const LumaPlace* pPlace,
                                 bool follow, LumaContentFxEffect* pOut) {
                    return startOn(pMod, "content.fx.start", actor, name, pJoint, pPlace, follow, pOut, 0);
                }

                // On the actor that draws the mod's model (content.model): its joints are the sockets.
                int32_t svcStartOnModel(const LumaMod* pMod, LumaContentModel model, LumaEffectName name, const char* pSocket,
                                        const LumaPlace* pPlace, bool follow, LumaContentFxEffect* pOut) {
                    const char* const pWhere = "content.fx.start_on_model";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    LiveActor* const pActor = luma::content::model::actorOf(pMod, model);
                    if (pActor == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    return startOn(pMod, pWhere, pActor, name, pSocket, pPlace, follow, pOut, model);
                }

                int32_t svcStop(const LumaMod* pMod, LumaContentFxEffect effect, bool now) {
                    const char* const pWhere = "content.fx.stop";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Effect* const pEffect = effectOf(pMod, effect);
                    if (pEffect == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (pEffect->shell != nullptr) {
                        if (now) {
                            pEffect->shell->emitter->forceDeleteEmitter();
                        } else {
                            pEffect->shell->emitter->deleteEmitter();
                        }
                    }
                    end(*pEffect, "stop");
                    return LUMA_OK;
                }

                int32_t svcTint(const LumaMod* pMod, LumaContentFxEffect effect, float scale, LumaColor rgba) {
                    const char* const pWhere = "content.fx.tint";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Effect* const pEffect = effectOf(pMod, effect);
                    if (pEffect == nullptr) {
                        report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (!(scale > 0.0f) || !std::isfinite(scale)) {
                        report(pMod, pWhere, 101u, "scale", nullptr, nullptr);
                        return LUMA_E_ARGUMENT;
                    }
                    Shell* const pShell = pEffect->shell;
                    if (pShell == nullptr) {
                        return LUMA_OK;
                    }
                    const u8 r = static_cast< u8 >(rgba >> 24);
                    const u8 g = static_cast< u8 >(rgba >> 16);
                    const u8 b = static_cast< u8 >(rgba >> 8);
                    const u8 a = static_cast< u8 >(rgba);
                    MultiEmitter* const pEmitter = pShell->emitter;
                    // The callback keeps the scale (each frame while it follows the scale) and the colour (each frame).
                    pEmitter->setBaseScale(scale);
                    pEmitter->setGlobalPrmColor(r, g, b, -1);
                    // Made already: an effect left where it started never takes the scale again, so its emitters do now;
                    // the alpha has no setter in the callback, so each emitter's own.
                    const Vec3f host = hostScaleOf(pShell);
                    for (s32 i = 0; i < pEmitter->mEmitters.size(); i++) {
                        SingleEmitter& rSingle = pEmitter->mEmitters[i];
                        if (rSingle.isValid()) {
                            EmitterCore* const pCore = rSingle.mEmitter->mEmitter;
                            if (!pShell->follow) {
                                pCore->setGlobalScale(Vec3f(host.x * scale, host.y * scale, host.z * scale));
                            }
                            pCore->mGlobalPrmClr.a = a;
                        }
                    }
                    return LUMA_OK;
                }

                const LumaContentFxService cTable = {
                    {sizeof(LumaContentFxService), LUMA_CONTENT_FX_MAJOR, LUMA_CONTENT_FX_MINOR},
                    svcStart,
                    svcStartOnModel,
                    svcStop,
                    svcTint,
                };

                // ---- The provider ----

                // The mod's stop (or its taking out): its effects go at once.
                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < sHigh; i++) {
                        Effect& rEffect = sEffects[i];
                        if (rEffect.handle != 0 && rEffect.mod == pMod) {
                            if (rEffect.shell != nullptr) {
                                rEffect.shell->emitter->forceDeleteEmitter();
                            }
                            end(rEffect, "mod");
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_CONTENT_FX_NAME,
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
                    Effect& rEffect = sEffects[i];
                    if (rEffect.handle == 0) {
                        continue;
                    }
                    Shell* const pShell = rEffect.shell;
                    LiveActor* const pActor = rEffect.actor;
                    if (rEffect.model != 0 && luma::content::model::actorOf(rEffect.mod, rEffect.model) == nullptr) {
                        // Its model was detached or ended: as the actor's own effects at its death.
                        if (pShell != nullptr) {
                            pShell->emitter->playCalcAndDeleteForeverEmitter();
                        }
                        end(rEffect, "model");
                        continue;
                    }
                    if (MR::isDead(pActor)) {
                        // As the actor's own effects (LiveActor::makeActorDead, EffectKeeper::clear).
                        if (pShell != nullptr) {
                            pShell->emitter->playCalcAndDeleteForeverEmitter();
                        }
                        end(rEffect, "actor");
                        continue;
                    }
                    if (pShell == nullptr) {
                        continue;  // nothing shows: nothing to follow or check
                    }
                    const bool clipped = MR::isClipped(pActor);
                    if (clipped != rEffect.clipped) {
                        if (clipped) {
                            pShell->emitter->stopEmitterOnClipped();
                        } else {
                            pShell->emitter->playEmitterOffClipped();
                        }
                        rEffect.clipped = clipped;
                    }
                    if (pShell->composed && pShell->follow) {
                        compose(pShell);
                    }
                    if (rEffect.checkEmpty && rEffect.frame != 0) {
                        check(rEffect);
                    }
                    if (facts && rEffect.frame < static_cast< u32 >(cFrames_alive_1)) {
                        aliveFact(rEffect);
                    }
                    rEffect.frame++;
                }
            }

            void released(const void* pStart, const void* pEnd) {
                if (sShells == nullptr && sLive == 0) {
                    return;
                }
                // The effect system going takes every emitter's particle records with it: nothing of ours is used again.
                const bool all = sSystem != nullptr && inRange(sSystem, pStart, pEnd);
                for (u32 i = 0; i < sHigh; i++) {
                    Effect& rEffect = sEffects[i];
                    if (rEffect.handle != 0 && (all || inRange(rEffect.actor, pStart, pEnd) ||
                                                (rEffect.shell != nullptr && inRange(rEffect.shell, pStart, pEnd)))) {
                        end(rEffect, "scene");
                    }
                }
                if (all) {
                    sShells = nullptr;
                    sSystem = nullptr;
                    return;
                }
                Shell** ppLink = &sShells;
                while (*ppLink != nullptr) {
                    Shell* const pShell = *ppLink;
                    if (inRange(pShell, pStart, pEnd) || inRange(pShell->actor, pStart, pEnd)) {
                        *ppLink = pShell->next;
                    } else {
                        ppLink = &pShell->next;
                    }
                }
            }

            const LumaServiceProvider* provider() { return &cProvider; }
        }
    }
}
