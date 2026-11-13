#include "Game/System/LumaMovie.hpp"

#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/LumaDraw.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/Table.hpp"
#include <luma_modlog.h>
#include <luma_movie.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <cstring>

namespace luma {
    namespace movie {
        namespace {
            // Content.MissingMovie (references 6, the content block 620-699).
            const u32 cMissingMovieCode = 673u;
            // The service's own rows, in the content block: no scene with a movie player; a file that is no movie.
            const u32 cNoSceneCode = 674u;
            const u32 cNotMovieCode = 675u;
            // Ends on their way at once: a stop, a play and another stop in one frame.
            const u32 kMostEnds = 4u;
            // NMOV's header (contract movies 4): its bytes, the codec word, the flags word (1 loops).
            const u32 kHeaderBytes = 0x40u;
            const u32 kCodecAt = 0x24u;
            const u32 kCodecAv1 = 1u;
            const u32 kFlagsAt = 0x2Cu;
            const u32 kFlagLoops = 1u;
            const u32 kFlagSkips = 2u;

            // The movie a mod plays: one at a time, as the scene has one player.
            struct Playing {
                LumaHandle handle;  // 0: none
                const LumaMod* mod;
                MoviePlayerSimple* player;  // the scene's, while it lasts
                LumaEventFn fn;
                void* user;
                bool loop;
                s32 frames;
            };

            // A LUMA_MOVIE_EVENT_ENDED on its way, with its own copy of what it says. Busy from its post to the start
            // of its deliver, or its drop.
            struct End {
                LumaEventNode node;
                bool busy;
                LumaHandle movie;
                s32 status;
                LumaEventFn fn;
                void* user;
            };

            const LumaServiceKernel* sKernel = nullptr;
            Playing sPlaying = {};
            LumaHandle sNextHandle = 0;
            End sEnds[kMostEnds];
            // The store path the player holds while it plays (MoviePlayerSimple keeps the pointer).
            char sPath[300];
            // A skipped movie ends as the game's in-stage ones (MoviePlayingSequence: PrologueA, FinalBattle, EpilogueB,
            // EndingB): the fade forced closed, kSkipEndWait frames on the black screen, then the fade opened in its
            // own time. The scene's player names the scene the wait belongs to (a new scene opens its own wipe).
            const s32 kSkipEndWait = 60;
            s32 sSkipWaitLeft = 0;
            const MoviePlayerSimple* sSkipWaitPlayer = nullptr;

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0) {
                const char* args[1] = {pArg0};
                sKernel->report(pMod, pWhere, code, args, pArg0 != nullptr ? 1u : 0u);
            }

            void say(const LumaMod* pMod, u32 code, const char* pRow, const char* pFound) {
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CONTENT;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = code;
                entry.mod = sKernel->mod_id(pMod);
                entry.file = "";
                entry.column = MR::getHashCode(pFound);
                entry.names[0] = "found";
                entry.values[0] = pFound;
                entry.count = 1;
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

            // The scene's movie player, or null: no scene runs (the game loads one), or this scene has none.
            MoviePlayerSimple* scenePlayer() {
                const GameSystemSceneController* const pController = SingletonHolder< GameSystem >::get()->mSceneController;
                if (pController == nullptr || !pController->isExistSceneObjHolder() || !pController->isNerveNormal()) {
                    return nullptr;
                }
                if (!MR::isExistSceneObj(SceneObj_MoviePlayerSimple)) {
                    return nullptr;
                }
                return MR::getSceneObj< MoviePlayerSimple >(SceneObj_MoviePlayerSimple);
            }

            void fact(const char* pWhat, LumaHandle handle, const char* pJson) {
                char key[48];
                nebula_snprintf(key, sizeof(key), "movie.%s.%llu", pWhat, static_cast< unsigned long long >(handle));
                luma::modlog::fact(key, pJson);
            }

            void deliverEnd(LumaEventNode* pNode) {
                End* const pEnd = reinterpret_cast< End* >(pNode);
                LumaEvent event = {};
                event.size = sizeof(event);
                event.kind = LUMA_MOVIE_EVENT_ENDED;
                event.status = pEnd->status;
                event.handle = pEnd->movie;
                const LumaEventFn fn = pEnd->fn;
                void* const pUser = pEnd->user;
                pEnd->busy = false;  // free again before the mod's function, which may play again
                (void)sKernel->call(pNode->mod, LUMA_MOVIE_NAME, fn, pUser, &event);
            }

            void dropEnd(LumaEventNode* pNode) {
                reinterpret_cast< End* >(pNode)->busy = false;
            }

            // The mod's movie is over (`status`, `pWhy` for the harness): no longer the mod's; its end posted when the
            // mod asked for one. Leaves the player alone (the caller stops it when it must).
            void finish(s32 status, const char* pWhy) {
                if (sPlaying.handle == 0) {
                    return;
                }
                const Playing done = sPlaying;
                sPlaying = Playing();
                if (luma::modlog::facts()) {
                    char json[64];
                    nebula_snprintf(json, sizeof(json), "{\"why\":\"%s\",\"status\":%d}", pWhy, status);
                    fact("end", done.handle, json);
                }
                if (done.fn == nullptr) {
                    return;
                }
                for (u32 i = 0; i < kMostEnds; i++) {
                    End& rEnd = sEnds[i];
                    if (rEnd.busy) {
                        continue;
                    }
                    rEnd.busy = true;
                    rEnd.node.mod = done.mod;
                    rEnd.node.deliver = deliverEnd;
                    rEnd.node.drop = dropEnd;
                    rEnd.movie = done.handle;
                    rEnd.status = status;
                    rEnd.fn = done.fn;
                    rEnd.user = done.user;
                    if (sKernel->post(&rEnd.node) != LUMA_OK) {
                        rEnd.busy = false;  // the mod is gone: no one to tell
                    }
                    return;
                }
                nebula_log("movie: no room for the end of %s's movie; it is not given\n", sKernel->mod_id(done.mod));
            }

            // ---- Subtitles (layer 4) ----

            // A table past this many rows is read to here (an hour of lines a second).
            const u32 kMostSubtitleRows = 4096u;

            // The movie whose .subs was read last (its path and its player), the table (mapped for the process) and its
            // columns, the movie's frame rate and frames; sSubsHave false when it has none or one that cannot be read.
            char sSubsMovie[300];
            const MoviePlayerSimple* sSubsPlayer = nullptr;
            ::Table sSubsTable;
            bool sSubsHave = false;
            s32 sSubsText = -1;
            s32 sSubsFrom = -1;
            s32 sSubsTo = -1;
            f32 sSubsRate = 0.0f;
            s32 sSubsFrames = 0;
            s32 sSubsMissing = -1;  // the row whose text key had no text, said once

            // `<movie>.subs`: the movie's path with its extension (after the last '/') replaced. False when too long.
            bool subsPath(const char* pMovie, char* pOut, u32 size) {
                const u32 length = static_cast< u32 >(std::strlen(pMovie));
                u32 stem = length;
                for (u32 i = length; i > 0; i--) {
                    if (pMovie[i - 1] == '/') {
                        break;
                    }
                    if (pMovie[i - 1] == '.') {
                        stem = i - 1;
                        break;
                    }
                }
                if (stem + sizeof(".subs") > size) {
                    return false;
                }
                std::memcpy(pOut, pMovie, stem);
                std::memcpy(pOut + stem, ".subs", sizeof(".subs"));
                return true;
            }

            // The cook's table checked against its file's length before a cell is read: its columns, rows and words
            // inside, each text cell inside the words and the words ending with a NUL.
            bool tableFits(const TableData* pData, u32 length) {
                if (length < sizeof(TableData) || pData->mColumns > 64u ||
                    sizeof(TableData) + pData->mColumns * sizeof(TableColumn) > length) {
                    return false;
                }
                const u64 cells = static_cast< u64 >(pData->mRows) * pData->mColumns * sizeof(u32);
                if (pData->mRowsAt > length || cells > length - pData->mRowsAt || pData->mTextAt > length ||
                    pData->mTextSize > length - pData->mTextAt || (pData->mRowsAt & 3u) != 0u) {
                    return false;
                }
                return pData->mTextSize == 0u || pData->text()[pData->mTextSize - 1u] == '\0';
            }

            // The .subs beside pMovie read into sSubs*. A file that is not the cook's table, or lacks its columns, is
            // named in the log once a movie and shows nothing.
            void readSubtitles(const MoviePlayerSimple* pPlayer, const char* pMovie) {
                const u32 nameLength = static_cast< u32 >(std::strlen(pMovie));
                sSubsPlayer = pPlayer;
                sSubsHave = false;
                sSubsMissing = -1;
                if (nameLength >= sizeof(sSubsMovie)) {
                    sSubsMovie[0] = '\0';
                    return;
                }
                std::memcpy(sSubsMovie, pMovie, nameLength + 1u);
                char path[sizeof(sSubsMovie) + 8];
                if (!subsPath(pMovie, path, sizeof(path))) {
                    return;
                }
                const s32 entry = GameAssets::find(path);
                GameAssets::File file = {};
                if (entry < 0 || !GameAssets::open(entry, &file)) {
                    return;
                }
                const void* const pFile = GameAssets::map(entry);
                const TableData* const pData = static_cast< const TableData* >(pFile);
                if (pFile == nullptr || file.mLength < 4u || std::memcmp(pFile, "NTBL", 4) != 0 || !tableFits(pData, file.mLength)) {
                    nebula_log("movie: %s is not a cooked table; the movie's subtitles are not shown\n", path);
                    return;
                }
                (void)sSubsTable.attach(pFile);
                sSubsText = sSubsTable.searchItemInfo("text");
                sSubsFrom = sSubsTable.searchItemInfo("from");
                sSubsTo = sSubsTable.searchItemInfo("to");
                if (sSubsText < 0 || sSubsFrom < 0 || sSubsTo < 0 || sSubsTable.getColumnKind("text") != TableKind_Text ||
                    sSubsTable.getColumnKind("from") != TableKind_Float || sSubsTable.getColumnKind("to") != TableKind_Float) {
                    nebula_log("movie: %s lacks its text, from and to columns; the movie's subtitles are not shown\n", path);
                    return;
                }
                // The movie's own clock: its frame rate and frames (contract movies 4).
                GameAssets::File movie = {};
                u8 header[kHeaderBytes];
                if (!GameAssets::open(pMovie, &movie) || movie.mLength < kHeaderBytes ||
                    GameAssets::read(&movie, header, static_cast< s32 >(kHeaderBytes), 0) != static_cast< s32 >(kHeaderBytes)) {
                    return;
                }
                u32 frames = 0u;
                f32 rate = 0.0f;
                std::memcpy(&frames, header + 4, sizeof(frames));
                std::memcpy(&rate, header + 8, sizeof(rate));
                if (!(rate > 0.0f) || !(rate <= 240.0f)) {
                    return;
                }
                sSubsRate = rate;
                sSubsFrames = static_cast< s32 >(frames);
                sSubsHave = true;
            }

            // ---- Calls ----

            int32_t svcPlay(const LumaMod* pMod, const char* pName, LumaEventFn fn, void* pUser, LumaMovie* pOut) {
                const char* const pWhere = "movie.play";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pName == nullptr || pName[0] == '\0') {
                    report(pMod, pWhere, 101u, "name");
                    return LUMA_E_ARGUMENT;
                }
                if (pOut == nullptr) {
                    report(pMod, pWhere, 101u, "out");
                    return LUMA_E_ARGUMENT;
                }
                MoviePlayerSimple* const pPlayer = scenePlayer();
                if (pPlayer == nullptr) {
                    report(pMod, pWhere, cNoSceneCode, nullptr);
                    return LUMA_E_BUSY;
                }
                char path[sizeof(sPath)];
                const s32 length = nebula_snprintf(path, sizeof(path), "/Luma/%s/%s.nmov", sKernel->mod_id(pMod), pName);
                if (length < 0 || static_cast< u32 >(length) >= sizeof(path)) {
                    report(pMod, pWhere, 101u, "name");
                    return LUMA_E_ARGUMENT;
                }
                GameAssets::File file = {};
                if (!GameAssets::open(path, &file)) {
                    say(pMod, cMissingMovieCode, "Content.MissingMovie", pName);
                    return LUMA_E_NOT_FOUND;
                }
                // The header: a cooked movie, its frames and whether it loops (contract movies 4).
                u8 header[kHeaderBytes];
                if (file.mLength < kHeaderBytes ||
                    GameAssets::read(&file, header, static_cast< s32 >(kHeaderBytes), 0) != static_cast< s32 >(kHeaderBytes) ||
                    std::memcmp(header, "NMOV", 4) != 0) {
                    report(pMod, pWhere, cNotMovieCode, pName);
                    return LUMA_E_FORMAT;
                }
                u32 frames = 0u;
                u32 flags = 0u;
                std::memcpy(&frames, header + 4, sizeof(frames));
                std::memcpy(&flags, header + kFlagsAt, sizeof(flags));
                // What the player plays now ends first: a mod's movie is told.
                if (pPlayer->isMovieActive()) {
                    finish(LUMA_E_CANCELLED, "other");
                    pPlayer->stopMovie();
                }
                std::memcpy(sPath, path, static_cast< u32 >(length) + 1u);
                const bool loop = (flags & kFlagLoops) != 0u;
                pPlayer->startMovie(sPath, loop);
                GameSceneFunction::requestPlayMovieDemo();
                sPlaying.handle = ++sNextHandle;
                sPlaying.mod = pMod;
                sPlaying.player = pPlayer;
                sPlaying.fn = fn;
                sPlaying.user = pUser;
                sPlaying.loop = loop;
                sPlaying.frames = static_cast< s32 >(frames);
                *pOut = sPlaying.handle;
                if (luma::modlog::facts()) {
                    char mod[160];
                    luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(pMod));
                    char name[300];
                    luma::modlog::jsonString(name, sizeof(name), pName);
                    char json[560];
                    nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"name\":%s,\"frames\":%u,\"loop\":%s}", mod, name,
                                    frames, loop ? "true" : "false");
                    fact("play", sPlaying.handle, json);
                }
                return LUMA_OK;
            }

            int32_t svcStop(const LumaMod* pMod, LumaMovie movie) {
                const char* const pWhere = "movie.stop";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (movie == 0 || sPlaying.handle != movie || sPlaying.mod != pMod) {
                    report(pMod, pWhere, 102u, nullptr);
                    return LUMA_E_HANDLE;
                }
                MoviePlayerSimple* const pPlayer = sPlaying.player;
                finish(LUMA_E_CANCELLED, "stop");
                if (pPlayer == scenePlayer() && pPlayer->isMovieActive()) {
                    pPlayer->stopMovie();
                }
                return LUMA_OK;
            }

            int32_t svcPosition(const LumaMod* pMod, LumaMovie movie, int32_t* pFrame, int32_t* pFrames) {
                const char* const pWhere = "movie.position";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (movie == 0 || sPlaying.handle != movie || sPlaying.mod != pMod || sPlaying.player != scenePlayer()) {
                    report(pMod, pWhere, 102u, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pFrame != nullptr) {
                    *pFrame = sPlaying.player->getCurrentFrame();
                }
                if (pFrames != nullptr) {
                    *pFrames = sPlaying.frames;
                }
                return LUMA_OK;
            }

            const LumaServiceRow cRows[] = {
                {cNoSceneCode, "Movie.NoScene", {"where", nullptr, nullptr, nullptr}},
                {cNotMovieCode, "Movie.NotMovie", {"where", "found", nullptr, nullptr}},
            };

            const LumaMovieService cTable = {
                {sizeof(LumaMovieService), LUMA_MOVIE_MAJOR, LUMA_MOVIE_MINOR},
                svcPlay,
                svcStop,
                svcPosition,
            };

            // ---- The provider ----

            // The mod's stop (or its taking out): its movie stops, the game goes on.
            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                if (sPlaying.handle == 0 || sPlaying.mod != pMod) {
                    return;
                }
                MoviePlayerSimple* const pPlayer = sPlaying.player;
                finish(LUMA_E_CANCELLED, "mod");
                if (pPlayer == scenePlayer() && pPlayer->isMovieActive()) {
                    pPlayer->stopMovie();
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_MOVIE_NAME,
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
            if (sSkipWaitLeft > 0) {
                if (scenePlayer() != sSkipWaitPlayer) {
                    sSkipWaitLeft = 0;  // the scene ended on the black screen: the next one opens its own wipe
                } else if (--sSkipWaitLeft == 0) {
                    MR::openWipeFade(-1);
                }
            }
            if (sPlaying.handle == 0) {
                return;
            }
            MoviePlayerSimple* const pPlayer = scenePlayer();
            if (pPlayer != sPlaying.player) {
                finish(LUMA_E_CANCELLED, "scene");  // the scene that played it ended
            } else if (!pPlayer->isMovieActive()) {
                finish(LUMA_OK, "end");
            } else if (pPlayer->getMovieName() != sPath) {
                finish(LUMA_E_CANCELLED, "other");  // the game played a movie of its own over it
            } else if (NebulaCutsceneSkip::takeMovieSkip()) {
                // The player skipped it (+, Yes; only a movie that may be skipped, isSkippable): it ends as the game's
                // own skipped movies end, and the mod goes on as after its end.
                finish(LUMA_OK, "skip");
                pPlayer->stopMovie();
                MR::forceCloseWipeFade();
                sSkipWaitLeft = kSkipEndWait;
                sSkipWaitPlayer = pPlayer;
            }
        }

        void drawSubtitles() {
            if (!MR::isExistSceneObj(SceneObj_MoviePlayerSimple)) {
                return;
            }
            const MoviePlayerSimple* const pPlayer = MR::getSceneObj< MoviePlayerSimple >(SceneObj_MoviePlayerSimple);
            const char* const pMovie = pPlayer->getMovieName();
            if (!pPlayer->isMovieActive() || pMovie == nullptr || pMovie[0] == '\0') {
                return;
            }
            if (pPlayer != sSubsPlayer || std::strcmp(pMovie, sSubsMovie) != 0) {
                readSubtitles(pPlayer, pMovie);
            }
            if (!sSubsHave) {
                return;
            }
            s32 frame = pPlayer->getCurrentFrame();
            if (frame < 0) {
                return;
            }
            if (sSubsFrames > 0) {
                frame %= sSubsFrames;  // a loop's lines again from its start
            }
            const f32 at = static_cast< f32 >(frame) / sSubsRate;
            // The line whose time holds `at`: the later row where two overlap.
            s32 row = -1;
            const s32 rows = sSubsTable.getNumEntries() < static_cast< s32 >(kMostSubtitleRows) ? sSubsTable.getNumEntries()
                                                                                              : static_cast< s32 >(kMostSubtitleRows);
            for (s32 r = 0; r < rows; r++) {
                f32 from = 0.0f;
                f32 to = 0.0f;
                (void)sSubsTable.getValueFast(r, sSubsFrom, &from);
                (void)sSubsTable.getValueFast(r, sSubsTo, &to);
                if (at >= from && at < to) {
                    row = r;
                }
            }
            if (row < 0) {
                return;
            }
            const char* pKey = nullptr;
            (void)sSubsTable.getValueFast(row, sSubsText, &pKey);
            // The words as the game finds a message: a mod's text row first (in the played language), Nebula's, the
            // game's own (with any mod's cooked message rows).
            TalkMessageInfo info = TalkMessageInfo();
            const char16_t* const pWords =
                pKey != nullptr && pKey[0] != '\0' && MessageSystem::getLayoutMessageDirect(&info, pKey) ? info.mMessage : nullptr;
            if (pWords == nullptr) {
                if (sSubsMissing != row) {
                    sSubsMissing = row;
                    nebula_log("movie: %s's subtitle %s is no message; it is not shown\n", sSubsMovie, pKey != nullptr ? pKey : "");
                }
                return;
            }
            luma::draw::subtitle(pWords);
        }

        bool hasSubtitles(const char* pMovieName) {
            char path[sizeof(sSubsMovie) + 8];
            return pMovieName != nullptr && subsPath(pMovieName, path, sizeof(path)) && GameAssets::find(path) >= 0;
        }

        bool isSkippable() {
            if (!MR::isExistSceneObj(SceneObj_MoviePlayerSimple)) {
                return true;
            }
            const MoviePlayerSimple* const pPlayer = MR::getSceneObj< MoviePlayerSimple >(SceneObj_MoviePlayerSimple);
            const char* const pMovie = pPlayer->getMovieName();
            if (!pPlayer->isMovieActive() || pMovie == nullptr || pMovie[0] == '\0') {
                return true;
            }
            GameAssets::File file = {};
            u8 header[kHeaderBytes];
            if (!GameAssets::open(pMovie, &file) || file.mLength < kHeaderBytes ||
                GameAssets::read(&file, header, static_cast< s32 >(kHeaderBytes), 0) != static_cast< s32 >(kHeaderBytes)) {
                return true;
            }
            u32 codec = 0u;
            u32 flags = 0u;
            std::memcpy(&codec, header + kCodecAt, sizeof(codec));
            std::memcpy(&flags, header + kFlagsAt, sizeof(flags));
            // The game's own movies (codec 0, the THP cook; no flags) skip as ever; a mod's (its cook's AV1) by its flag.
            return codec != kCodecAv1 || (flags & kFlagSkips) != 0u;
        }

        const LumaServiceProvider* provider() {
            return &cProvider;
        }
    }  // namespace movie
}  // namespace luma
