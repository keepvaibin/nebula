#pragma once

#include "Game/LiveActor/Spine.hpp"
#include "Game/Util/Tunable.hpp"

/// @brief Used for executing states of a LiveActor.
class Nerve {
public:
    /// @brief Executes a state based on the host actor.
    /// @param pSpine The spine that contains the host LiveActor to execute the state for.
    virtual void execute(Spine* pSpine) const = 0;
    /// @brief Executes after the last iteration of a state before it executes another state.
    /// @param pSpine The spine that contains the host LiveActor to execute the state for.
    virtual void executeOnEnd(Spine* pSpine) const;

    // For mods (NerveRegistry.cpp): its record's number + 1 once the records are indexed, and its plugin hooks' list + 1.
    // Every nerve is a static object of its own class, constant-initialized, so these start at 0.
    mutable u16 mNebulaRecord = 0;
    mutable u16 mNebulaHook = 0;
};

class LiveActor;
template < typename T >
class ActorStateBase;

namespace nebula {
    // A nerve as mods name it: "<mClass>.<the nerve's name after its "Nrv">" ("Kuribo.Walk"); NEW_NERVE writes one per
    // nerve into the "nebnrv" section. mActorOf: the actor a spine running it belongs to (the executor if its class is a
    // LiveActor, a state's host if that is one), else null.
    struct NerveRecord {
        const Nerve* mNerve;
        const char* mClass;
        const char* mName;
        LiveActor* (*mActorOf)(void* pExecutor);
    };

    inline void* nerveStateHost(...) {
        return nullptr;
    }

    template < typename T >
    T* nerveStateHost(ActorStateBase< T >* pState) {
        return pState->getHost();
    }

    template < typename P >
    LiveActor* nerveLiveActor(P* pObject) {
        if constexpr (__is_base_of(LiveActor, P)) {
            return pObject;
        } else {
            return nullptr;
        }
    }

    template < typename C >
    LiveActor* nerveActorOf(void* pExecutor) {
        C* const pObject = reinterpret_cast< C* >(pExecutor);
        if constexpr (__is_base_of(LiveActor, C)) {
            return pObject;
        } else {
            return nerveLiveActor(nerveStateHost(pObject));
        }
    }

    enum NervePhase : s32 {
        NervePhase_Enter = 0,
        NervePhase_Frame,
        NervePhase_Leave,
    };

    // Set by the mods' code: a nerve with hooks (mNebulaHook != 0) reaches it on entering, each frame and leaving. For
    // NervePhase_Frame, false: a plugin did this frame in the game's place.
    extern bool (*gNerveHook)(Spine* pSpine, const Nerve* pNerve, s32 phase);

    // The records, once indexed (indexNerves, on first use): each nerve's mNebulaRecord names its record. A nerve defined
    // in a header has a record in every file that includes it; those after the first are skipped. A nerve's key is
    // "<class>.<short name>", the short name its name after "Nrv", else without a leading "HostType" or class name.
    void indexNerves();
    s32 nerveCount();
    const NerveRecord* nerveAt(s32 index);
    const char* nerveKeyAt(s32 index);
    const NerveRecord* nerveRecordOf(const Nerve* pNerve);
    const char* nerveKeyOf(const Nerve* pNerve);
    // By key. Null: no such nerve.
    const NerveRecord* findNerve(const char* pKey);
}  // namespace nebula

#if defined(_WIN32)
#pragma section("nebnrv$m", read)
#define NEBULA_NERVE_SECTION __declspec(allocate("nebnrv$m"))
#elif defined(__APPLE__)
#define NEBULA_NERVE_SECTION __attribute__((section("__DATA,nebnrv")))
#else
#define NEBULA_NERVE_SECTION __attribute__((section("nebnrv")))
#endif

#define NEBULA_NERVE_RECORD(name, parent_class)                                                                                                     \
    NEBULA_NERVE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::NerveRecord name##_nebula_nerve = {                                            \
        &name::sInstance, #parent_class, #name, &::nebula::nerveActorOf< parent_class >};

#define NEW_NERVE(name, parent_class, executor_name)                                                                                                 \
    class name : public Nerve {                                                                                                                      \
    public:                                                                                                                                          \
        virtual void execute(Spine* pSpine) const {                                                                                                  \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->exe##executor_name();                                                                                                             \
        };                                                                                                                                           \
        static name sInstance;                                                                                                                       \
    };                                                                                                                                               \
    ATTRIBUTE_WEAK_DEF(name name::sInstance);                                                                                                        \
    NEBULA_NERVE_RECORD(name, parent_class)

#define NEW_NERVE_ONEND(name, parent_class, executor_name, executorOnEnd_name)                                                                       \
    class name : public Nerve {                                                                                                                      \
    public:                                                                                                                                          \
        virtual void execute(Spine* pSpine) const {                                                                                                  \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->exe##executor_name();                                                                                                             \
        };                                                                                                                                           \
        virtual void executeOnEnd(Spine* pSpine) const {                                                                                             \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->end##executorOnEnd_name();                                                                                                        \
        };                                                                                                                                           \
        static name sInstance;                                                                                                                       \
    };                                                                                                                                               \
    ATTRIBUTE_WEAK_DEF(name name::sInstance);                                                                                                        \
    NEBULA_NERVE_RECORD(name, parent_class)

/* easy alternative to get a nerve instance (in the standard format) */
#define GET_NERVE(cls, nerve) (&Nrv##cls::nerve::sInstance)

/* easy alternative to get a nerve instance (with an anonymous namespace) */
#define GET_NERVE_ANON(nerve) (&::nerve::sInstance)

/* easy alternative to get a nerve instance (with no namespace) */
#define GET_NERVE_GLOBAL(nerve) (&nerve::sInstance)

/* easy alternative to get a nerve instance (where namespace does not follow standard) */
#define GET_NERVE_DIRECT(nmspc, nerve) (&nmspc::nerve::sInstance)
