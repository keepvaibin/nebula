#include "Game/System/LumaDrawPost.hpp"

#include "Game/Render/FrameList.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/HashUtil.hpp"
#include <luma_draw_post.h>
#include <luma_modlog.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <cstring>

// The draw.post service (plan 2.4 layer 8; contract draw 15). Game thread throughout.
//
// A program is a slot of one table across mods: the host's compiled program (post_register over the text of the mod's
// cooked <name>.post), what it reads and its params' defaults. A mod has one slot in each stage: the program it runs
// there and its 16 parameter floats. Each frame, at each stage, the mods' slots in Mods-list order (bottom first) each
// append one NEBULA_PASS_POST: the host's program, and its four parameter rows in the list's instance rows.
//
// No cost with no mod: stage() is one load while no slot is set there.

namespace luma {
    namespace draw {
        namespace post {
            namespace {
                const u32 kPrograms = 64;      // programs of all mods at once
                const u32 kSlots = 32;         // mods with a program in one stage
                const u32 kParams = LUMA_DRAW_POST_PARAMS_MOST;
                const u32 cMissingCode = 540;  // Draw.Post.Missing {found}
                const u32 cCompileCode = 541;  // Draw.Post.Compile {found, detail}
                const u32 cNoMotionCode = 542; // Draw.Post.NoMotion {found}
                const u32 cEmptyCode = 543;    // Draw.Post.Empty {where}: params for a stage it runs nothing in

                struct Program {
                    LumaHandle handle;  // 0: a free slot
                    const LumaMod* mod;
                    u32 host;           // the host's program
                    u32 reads;          // LUMA_DRAW_POST_READS_*
                    f32 defaults[kParams];
                };

                struct Slot {
                    const LumaMod* mod;  // null: free
                    LumaHandle program;
                    s32 order;           // the mod's place in the Mods list, this frame
                    f32 params[kParams];
                };

                const LumaServiceKernel* sKernel = nullptr;
                Program sPrograms[kPrograms];
                LumaHandle sNextHandle = 0;
                Slot sSlots[LUMA_DRAW_POST_STAGES][kSlots];
                u32 sUsed[LUMA_DRAW_POST_STAGES];  // slots set in each stage
                Slot sCalling[kSlots];

                void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0) {
                    const char* args[1] = {pArg0};
                    sKernel->report(pMod, pWhere, code, args, pArg0 != nullptr ? 1u : 0u);
                }

                s32 badArgument(const LumaMod* pMod, const char* pWhere, const char* pWhat) {
                    report(pMod, pWhere, 101u, pWhat);
                    return LUMA_E_ARGUMENT;
                }

                // A refusal of draw's block (540 to 542) in the Mods page's words.
                void say(const LumaMod* pMod, u32 code, const char* pRow, const char* pName, const char* pDetail) {
                    LumaModLogEntry entry = {};
                    entry.size = sizeof(entry);
                    entry.source = LUMA_MODLOG_CONTENT;
                    entry.level = LUMA_MODLOG_ERROR;
                    entry.code = code;
                    entry.mod = sKernel->mod_id(pMod);
                    entry.file = "";
                    entry.column = MR::getHashCode(pName);
                    entry.names[0] = "found";
                    entry.values[0] = pName;
                    entry.count = 1;
                    if (pDetail != nullptr) {
                        entry.names[1] = "detail";
                        entry.values[1] = pDetail;
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
                        report(pMod, pWhere, 100u, nullptr);
                        return LUMA_E_THREAD;
                    }
                    return LUMA_OK;
                }

                Program* programOf(const LumaMod* pMod, LumaHandle handle) {
                    if (handle == 0) {
                        return nullptr;
                    }
                    for (u32 i = 0; i < kPrograms; i++) {
                        if (sPrograms[i].handle == handle) {
                            return sPrograms[i].mod == pMod ? &sPrograms[i] : nullptr;
                        }
                    }
                    return nullptr;
                }

                Slot* slotOf(const LumaMod* pMod, u32 stage) {
                    for (u32 i = 0; i < kSlots; i++) {
                        if (sSlots[stage][i].mod == pMod) {
                            return &sSlots[stage][i];
                        }
                    }
                    return nullptr;
                }

                void freeSlot(u32 stage, Slot& rSlot) {
                    rSlot = Slot();
                    sUsed[stage]--;
                }

                void freeProgram(Program& rProgram) {
                    for (u32 stage = 0; stage < LUMA_DRAW_POST_STAGES; stage++) {
                        Slot* const pSlot = slotOf(rProgram.mod, stage);
                        if (pSlot != nullptr && pSlot->program == rProgram.handle) {
                            freeSlot(stage, *pSlot);
                        }
                    }
                    const NebulaHostServices* const pHost = nebula_host();
                    if (pHost->post_free != nullptr) {
                        pHost->post_free(pHost->user, rProgram.host);
                    }
                    rProgram = Program();
                }

                u32 readU32(const u8* p) {
                    return static_cast< u32 >(p[0]) | static_cast< u32 >(p[1]) << 8 | static_cast< u32 >(p[2]) << 16 |
                           static_cast< u32 >(p[3]) << 24;
                }

                f32 readF32(const u8* p) {
                    const u32 bits = readU32(p);
                    f32 value;
                    memcpy(&value, &bits, sizeof(value));
                    return value;
                }

                // The cooked file read: false when it is not one (the cook's fault, said as missing). The text is
                // `*ppText`, `*pTextBytes`; the defaults in place.
                bool readFile(const u8* pFile, u32 length, u32* pReads, f32* pDefaults, const u8** ppText, u32* pTextBytes) {
                    if (length < LUMA_DRAW_POST_HEADER_BYTES || memcmp(pFile, LUMA_DRAW_POST_FILE_MAGIC, 8) != 0 ||
                        readU32(pFile + 8) != LUMA_DRAW_POST_FILE_VERSION) {
                        return false;
                    }
                    const u32 reads = readU32(pFile + 12);
                    const u32 count = readU32(pFile + 16);
                    const u32 textBytes = readU32(pFile + 20);
                    if ((reads & ~(LUMA_DRAW_POST_READS_DEPTH | LUMA_DRAW_POST_READS_MOTION)) != 0 || count > kParams ||
                        textBytes == 0 ||
                        static_cast< u64 >(LUMA_DRAW_POST_HEADER_BYTES) + static_cast< u64 >(count) * LUMA_DRAW_POST_PARAM_BYTES +
                                textBytes != length) {
                        return false;
                    }
                    for (u32 i = 0; i < kParams; i++) {
                        pDefaults[i] = 0.0f;
                    }
                    const u8* pParam = pFile + LUMA_DRAW_POST_HEADER_BYTES;
                    for (u32 i = 0; i < count; i++, pParam += LUMA_DRAW_POST_PARAM_BYTES) {
                        const u32 floats = readU32(pParam + 32);
                        const u32 row = readU32(pParam + 36);
                        const u32 column = readU32(pParam + 40);
                        if (floats == 0 || floats > 4 || row > 3 || column + floats > 4) {
                            return false;
                        }
                        for (u32 k = 0; k < floats; k++) {
                            pDefaults[row * 4 + column + k] = readF32(pParam + 44 + k * 4);
                        }
                    }
                    *pReads = reads;
                    *ppText = pParam;
                    *pTextBytes = textBytes;
                    return true;
                }

                void slotFact(u32 stage, const LumaMod* pMod, LumaHandle program) {
                    if (!luma::modlog::facts()) {
                        return;
                    }
                    char key[32];
                    nebula_snprintf(key, sizeof(key), "draw.post.%u", stage);
                    char mod[80];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(pMod));
                    char json[128];
                    nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"program\":%llu}", mod,
                                    static_cast< unsigned long long >(program));
                    luma::modlog::fact(key, json);
                }

                bool finite(f32 value) {
                    return value == value && value - value == 0.0f;
                }

                // ---- Calls ----

                int32_t svcProgram(const LumaMod* pMod, const char* pName, LumaDrawPostProgram* pOut) {
                    const char* const pWhere = "draw.post.program";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (pName == nullptr || pName[0] == 0 || strlen(pName) > 200) {
                        return badArgument(pMod, pWhere, "name");
                    }
                    if (pOut == nullptr) {
                        return badArgument(pMod, pWhere, "out");
                    }
                    u32 own = 0;
                    Program* pFree = nullptr;
                    for (u32 i = 0; i < kPrograms; i++) {
                        if (sPrograms[i].handle == 0) {
                            if (pFree == nullptr) {
                                pFree = &sPrograms[i];
                            }
                        } else if (sPrograms[i].mod == pMod) {
                            own++;
                        }
                    }
                    if (own >= LUMA_DRAW_POST_PROGRAMS_MOST || pFree == nullptr) {
                        char most[16];
                        nebula_snprintf(most, sizeof(most), "%u", own >= LUMA_DRAW_POST_PROGRAMS_MOST ? LUMA_DRAW_POST_PROGRAMS_MOST : kPrograms);
                        report(pMod, pWhere, 104u, most);
                        return LUMA_E_LIMIT;
                    }
                    char path[224];
                    nebula_snprintf(path, sizeof(path), pName[0] == '/' ? "%s.post" : "/post/%s.post", pName);
                    const s32 entry = GameAssets::find(path);
                    GameAssets::File file;
                    file.mEntry = -1;
                    file.mLength = 0;
                    const u8* const pFile =
                        entry < 0 || !GameAssets::open(entry, &file) ? nullptr : static_cast< const u8* >(GameAssets::map(entry));
                    Program program = {};
                    const u8* pText = nullptr;
                    u32 textBytes = 0;
                    if (pFile == nullptr || !readFile(pFile, file.mLength, &program.reads, program.defaults, &pText, &textBytes)) {
                        say(pMod, cMissingCode, "Draw.Post.Missing", pName, nullptr);
                        return LUMA_E_NOT_FOUND;
                    }
                    const NebulaHostServices* const pHost = nebula_host();
                    if (pHost->post_register == nullptr) {
                        return LUMA_E_ABSENT;  // a host without post programs (never with API 152)
                    }
                    char error[512];
                    error[0] = '\0';
                    u32 host = 0;
                    const s32 result = pHost->post_register(pHost->user, pText, textBytes, program.reads, &host, error,
                                                            sizeof(error));
                    if (result == NEBULA_POST_NO_MOTION) {
                        say(pMod, cNoMotionCode, "Draw.Post.NoMotion", pName, nullptr);
                        return LUMA_E_ABSENT;
                    }
                    if (result != NEBULA_POST_OK || host == 0) {
                        error[sizeof(error) - 1] = '\0';
                        say(pMod, cCompileCode, "Draw.Post.Compile", pName, error[0] != '\0' ? error : "?");
                        return LUMA_E_ARGUMENT;
                    }
                    program.handle = ++sNextHandle;
                    program.mod = pMod;
                    program.host = host;
                    *pFree = program;
                    *pOut = program.handle;
                    return LUMA_OK;
                }

                int32_t svcSet(const LumaMod* pMod, LumaDrawPostStage stage, LumaDrawPostProgram handle) {
                    const char* const pWhere = "draw.post.set";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (static_cast< u32 >(stage) >= LUMA_DRAW_POST_STAGES) {
                        return badArgument(pMod, pWhere, "stage");
                    }
                    const u32 s = static_cast< u32 >(stage);
                    Slot* pSlot = slotOf(pMod, s);
                    if (handle == 0) {
                        if (pSlot != nullptr) {
                            freeSlot(s, *pSlot);
                        }
                        slotFact(s, pMod, 0);
                        return LUMA_OK;
                    }
                    const Program* const pProgram = programOf(pMod, handle);
                    if (pProgram == nullptr) {
                        report(pMod, pWhere, 102u, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    if (pSlot == nullptr) {
                        pSlot = slotOf(nullptr, s);
                        if (pSlot == nullptr) {
                            char most[16];
                            nebula_snprintf(most, sizeof(most), "%u", kSlots);
                            report(pMod, pWhere, 104u, most);
                            return LUMA_E_LIMIT;
                        }
                        pSlot->mod = pMod;
                        sUsed[s]++;
                    }
                    pSlot->program = handle;
                    memcpy(pSlot->params, pProgram->defaults, sizeof(pSlot->params));
                    slotFact(s, pMod, handle);
                    return LUMA_OK;
                }

                int32_t svcParams(const LumaMod* pMod, LumaDrawPostStage stage, const float* pValues, uint32_t count) {
                    const char* const pWhere = "draw.post.params";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (static_cast< u32 >(stage) >= LUMA_DRAW_POST_STAGES) {
                        return badArgument(pMod, pWhere, "stage");
                    }
                    if (count > kParams) {
                        return badArgument(pMod, pWhere, "count");
                    }
                    if (count != 0 && pValues == nullptr) {
                        return badArgument(pMod, pWhere, "values");
                    }
                    for (u32 i = 0; i < count; i++) {
                        if (!finite(pValues[i])) {
                            return badArgument(pMod, pWhere, "values");
                        }
                    }
                    Slot* const pSlot = slotOf(pMod, static_cast< u32 >(stage));
                    if (pSlot == nullptr) {
                        report(pMod, pWhere, cEmptyCode, nullptr);
                        return LUMA_E_STATE;
                    }
                    if (count != 0) {
                        memcpy(pSlot->params, pValues, count * sizeof(f32));
                    }
                    return LUMA_OK;
                }

                int32_t svcProgramFree(const LumaMod* pMod, LumaDrawPostProgram handle) {
                    const char* const pWhere = "draw.post.program_free";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    Program* const pProgram = programOf(pMod, handle);
                    if (pProgram == nullptr) {
                        report(pMod, pWhere, 102u, nullptr);
                        return LUMA_E_HANDLE;
                    }
                    freeProgram(*pProgram);
                    return LUMA_OK;
                }

                const LumaDrawPostService cTable = {
                    {sizeof(LumaDrawPostService), LUMA_DRAW_POST_MAJOR, LUMA_DRAW_POST_MINOR},
                    svcProgram,
                    svcSet,
                    svcParams,
                    svcProgramFree,
                };

                // ---- The provider ----

                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    for (u32 i = 0; i < kPrograms; i++) {
                        if (sPrograms[i].handle != 0 && sPrograms[i].mod == pMod) {
                            freeProgram(sPrograms[i]);  // its slots with it
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                // Its report row; Draw.Post.Missing, .Compile and .NoMotion are said through the mod log (say).
                const LumaServiceRow cRows[] = {
                    {cEmptyCode, "Draw.Post.Empty", {"where", nullptr, nullptr, nullptr}},
                };

                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_DRAW_POST_NAME,
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

            void stage(u32 stage) {
                if (stage >= LUMA_DRAW_POST_STAGES || sUsed[stage] == 0) {
                    return;
                }
                // Bottom of the Mods list first (as the draw points): the top one's program runs last, over the others.
                u32 count = 0;
                for (u32 i = 0; i < kSlots; i++) {
                    const Slot& rSlot = sSlots[stage][i];
                    if (rSlot.mod != nullptr && sKernel->mod_live(rSlot.mod) != 0) {
                        sCalling[count] = rSlot;
                        sCalling[count].order = sKernel->mod_order(rSlot.mod);
                        count++;
                    }
                }
                for (u32 i = 1; i < count; i++) {
                    const Slot moving = sCalling[i];
                    u32 j = i;
                    while (j > 0 && sCalling[j - 1].order < moving.order) {
                        sCalling[j] = sCalling[j - 1];
                        j--;
                    }
                    sCalling[j] = moving;
                }
                for (u32 i = 0; i < count; i++) {
                    const Program* const pProgram = programOf(sCalling[i].mod, sCalling[i].program);
                    if (pProgram != nullptr) {
                        Frame::post(pProgram->host, reinterpret_cast< const f32(*)[4] >(sCalling[i].params));
                    }
                }
            }

            const LumaServiceProvider* provider() {
                return &cProvider;
            }
        }  // namespace post
    }  // namespace draw
}  // namespace luma
