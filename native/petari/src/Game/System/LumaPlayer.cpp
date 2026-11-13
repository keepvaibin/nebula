#include "Game/System/LumaPlayer.hpp"

#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <luma.h>
#include <luma_player.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <cmath>

// The player service (contract services-player). Game thread throughout: the calls, the provider, NebulaArena::destroy.
//
// An extra player is a MarioActor made as the scene's own is (init2, then initAfterPlacement) but marked extra: the
// scene's player stays the one the MarioHolder, the camera, the HUD, the light director and the game over follow. Its
// control reads the controller its channel names (MR::PlayerChannelScope); its deaths and death-area falls bring it back
// at its home (MarioActor::respawnExtra). It is made in the scene's arena, in the main stage's zone, so it goes with the
// scene: released() forgets it then.
//
// No cost with no mod: released() is one load while no extra player stands.

namespace luma {
    namespace player {
        namespace {
            struct Extra {
                const LumaMod* mod;  // null: a free slot
                MarioActor* actor;
                bool pad;            // driven by the second controller (holds the co-star's reads)
            };

            const LumaServiceKernel* sKernel = nullptr;
            Extra sExtras[LUMA_PLAYER_MOST_EXTRA];
            u32 sLive = 0;  // extra players standing

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

            // A scene that runs (past its start, its objects' arena there) with its player.
            bool sceneWithPlayer() {
                const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
                const GameSystemSceneController* const pController = pSystem != nullptr ? pSystem->mSceneController : nullptr;
                return Arenas::scene() != nullptr && pController != nullptr && pController->isExistSceneObjHolder() &&
                       pController->isNerveNormal() && MR::isExistSceneObj(SceneObj_MarioHolder) &&
                       MR::getMarioHolder()->getMarioActor() != nullptr && MR::isExistSceneObj(SceneObj_AllLiveActorGroup);
            }

            // The main stage's zone while the player is made (its connections and areas are the main stage's).
            struct InMainStage {
                s32 mBefore;
                InMainStage() : mBefore(MR::getCurrentPlacementZoneId()) { MR::setCurrentPlacementZoneId(0); }
                ~InMainStage() {
                    if (mBefore < 0) {
                        MR::clearCurrentPlacementZoneId();
                    } else {
                        MR::setCurrentPlacementZoneId(mBefore);
                    }
                }
            };

            bool finite3(const float* pValues) {
                return std::isfinite(pValues[0]) && std::isfinite(pValues[1]) && std::isfinite(pValues[2]);
            }

            Extra* extraOf(const LumaMod* pMod, const void* pActor) {
                for (Extra& rExtra : sExtras) {
                    if (rExtra.mod != nullptr && rExtra.actor == pActor) {
                        return rExtra.mod == pMod ? &rExtra : nullptr;
                    }
                }
                return nullptr;
            }

            // The slot goes; the player stays dead in its scene (its memory is the scene's).
            void forget(Extra& rExtra) {
                if (rExtra.pad) {
                    MR::takeCoStar(false);
                }
                rExtra = Extra{};
                sLive--;
            }

            int32_t svcMake(const LumaMod* pMod, const LumaPlayerInit* pInit, LumaActor* pOut) {
                const char* const pWhere = "player.make";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pInit == nullptr || pOut == nullptr) {
                    report(pMod, pWhere, 101u, pInit == nullptr ? "init" : "out", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (pInit->size < sizeof(LumaPlayerInit)) {
                    char found[16];
                    char expected[16];
                    nebula_snprintf(found, sizeof(found), "%u", static_cast< u32 >(pInit->size));
                    nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaPlayerInit)));
                    report(pMod, pWhere, 103u, "LumaPlayerInit", found, expected);
                    return LUMA_E_ARGUMENT;
                }
                const u32 input = static_cast< u32 >(pInit->input);
                const char* pWrong = input > static_cast< u32 >(LUMA_PLAYER_INPUT_NONE)                            ? "input"
                                     : input == static_cast< u32 >(LUMA_PLAYER_INPUT_PAD) ? (pInit->pad != 1 ? "pad" : nullptr)
                                     : pInit->pad != 0                                                            ? "pad"
                                     : nullptr;
                if (pWrong == nullptr && !finite3(pInit->pos)) {
                    pWrong = "pos";
                }
                if (pWrong == nullptr && !finite3(pInit->rot)) {
                    pWrong = "rot";
                }
                if (pWrong != nullptr) {
                    report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (!sceneWithPlayer()) {
                    return LUMA_E_STATE;
                }
                Extra* pSlot = nullptr;
                for (Extra& rExtra : sExtras) {
                    if (rExtra.mod == nullptr) {
                        pSlot = &rExtra;
                        break;
                    }
                }
                if (pSlot == nullptr) {
                    char most[16];
                    nebula_snprintf(most, sizeof(most), "%u", static_cast< u32 >(LUMA_PLAYER_MOST_EXTRA));
                    report(pMod, pWhere, 104u, most, nullptr, nullptr);
                    return LUMA_E_LIMIT;
                }
                const bool pad = input == static_cast< u32 >(LUMA_PLAYER_INPUT_PAD);
                const Vec3f pos(pInit->pos[0], pInit->pos[1], pInit->pos[2]);
                // A placement's turn, as MarioActor::init takes it.
                const Vec3f rot(MR::repeat(pInit->rot[0], 0.0f, 360.0f), MR::repeat(pInit->rot[1], 0.0f, 360.0f),
                                MR::repeat(pInit->rot[2], 0.0f, 360.0f));
                AllLiveActorGroup* const pActors = MR::getAllLiveActorGroup();
                // Actors registered and not yet given a placement's system info belong to a placement being made: ours
                // then get its, as its own objects do. Otherwise ours are taken as given none (no view group).
                const bool pending = pActors->mActorNum < pActors->getObjNum();
                MarioActor* pActor = nullptr;
                {
                    const NebulaArena::CurrentScope scope(Arenas::scene());
                    const InMainStage inMainStage;
                    pActor = new MarioActor("LumaExtraPlayer", true);
                    pActor->mInputChannel = pad ? PLAYER_2 : -1;
                    pActor->mExtraHome = pos;
                    pActor->mExtraHomeRotate = rot;
                    pActor->init2(pos, rot, 1);
                    pActor->initAfterPlacement();
                }
                if (!pending) {
                    pActors->mActorNum = pActors->getObjNum();
                }
                if (pad) {
                    MR::takeCoStar(true);
                }
                *pSlot = Extra{pMod, pActor, pad};
                sLive++;
                *pOut = pActor;
                return LUMA_OK;
            }

            int32_t svcRemove(const LumaMod* pMod, LumaActor player) {
                const char* const pWhere = "player.remove";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Extra* const pExtra = player != nullptr ? extraOf(pMod, player) : nullptr;
                if (pExtra == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                pExtra->actor->endExtra();
                forget(*pExtra);
                return LUMA_OK;
            }

            const LumaPlayerService cTable = {
                {sizeof(LumaPlayerService), LUMA_PLAYER_MAJOR, LUMA_PLAYER_MINOR},
                svcMake,
                svcRemove,
            };

            // ---- The provider ----

            // The mod's stop (or its taking out): its extra players go as remove takes them.
            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                for (Extra& rExtra : sExtras) {
                    if (rExtra.mod == pMod) {
                        rExtra.actor->endExtra();
                        forget(rExtra);
                    }
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_PLAYER_NAME,
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

        void released(const void* pStart, const void* pEnd) {
            if (sLive == 0) {
                return;
            }
            for (Extra& rExtra : sExtras) {
                const char* const pAt = static_cast< const char* >(static_cast< const void* >(rExtra.actor));
                if (rExtra.mod != nullptr && pAt >= static_cast< const char* >(pStart) && pAt < static_cast< const char* >(pEnd)) {
                    forget(rExtra);
                }
            }
        }

        const LumaServiceProvider* provider() { return &cProvider; }
    }
}
