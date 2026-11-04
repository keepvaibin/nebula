// With the game's headers: a new object written the way the game's own are, and a hook on one of its functions.
#include <nebula_game_sdk.h>

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util.hpp"

namespace {
    const NebulaModApi* game;
    const NebulaPlugin* me;

    // Floats up and down over where it is put.
    class Pulse : public LiveActor {
    public:
        Pulse(const char* pName) : LiveActor(pName), mBase(0.0f), mTime(0) {}

        virtual void init(const JMapInfoIter& rIter) {
            if (rIter.isValid()) {
                MR::initDefaultPos(this, rIter);
            }
            mBase = mPosition.y;
            MR::connectToSceneMapObjMovement(this);
            MR::invalidateClipping(this);
            makeActorAppeared();
            game->log(me, "Pulse made");
        }

        virtual void control() {
            mPosition.y = mBase + 50.0f * sinf(0.05f * static_cast< f32 >(mTime++));
            if (mTime == 30) {
                game->log(me, "Pulse moved");
            }
        }

        f32 mBase;
        s32 mTime;
    };

    void* createPulse(const char* pName) {
        return new Pulse(pName);
    }

    // Every other hit does nothing.
    void (*originalDecLife)(MarioActor*, u16);
    s32 hits;

    void decLifeHook(MarioActor* pSelf, u16 amount) {
        if (amount == 0 && (hits++ & 1) == 1) {
            game->log(me, "a hit let off");
            return;
        }
        originalDecLife(pSelf, amount);
    }

    int32_t stageFrame(const NebulaModEventData* pData, void*) {
        static s32 frames;
        MarioActor* pPlayer = MarioAccess::getPlayerActor();
        if (++frames == 300) {
            // The game's own calls, directly.
            TVec3f at = pPlayer->mPosition;
            at.x += 400.0f;
            const f32 where[3] = {at.x, at.y, at.z};
            game->spawn("Pulse", where);
            game->log(me, MR::isOnGroundPlayer() ? "direct: on the ground" : "direct: in the air");
        }
        if (frames == 340 || frames == 360) {
            const u32 before = pPlayer->getHealth();
            pPlayer->decLife(0);
            game->log(me, pPlayer->getHealth() == before ? "life kept" : "life lost");
        }
        return pData->value;
    }
}  // namespace

NEBULA_GAME_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->register_creator("Pulse", createPulse, nullptr);
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stageFrame, nullptr);
    void* pTarget = game->find_function("?decLife@MarioActor@@QEAAXG@Z");
    if (pTarget == nullptr || game->hook(pTarget, reinterpret_cast< void* >(decLifeHook), reinterpret_cast< void** >(&originalDecLife)) != 0) {
        game->log(me, "no hook on decLife");
    }
}
