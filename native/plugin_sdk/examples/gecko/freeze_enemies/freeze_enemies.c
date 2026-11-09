/* Freeze enemies (Gecko census, MOD-PLAN M9): Goombas, Koopas and the other walking enemies stand still. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

/* Every frame of a walking enemy's nerves: the game's frame is not run. */
static int32_t hold(NebulaActor* actor, const char* nerve, NebulaNervePhase phase, int32_t frames, void* user) {
    (void)actor;
    (void)nerve;
    (void)frames;
    (void)user;
    return phase == NEBULA_NERVE_FRAME ? 0 : 1;
}

/* Classes whose nerves are held: every nerve of each ("<class>." names). */
static const char* const classes[] = {"Kuribo.", "KuriboChief.", "NokonokoLand.", "Karon.", "Teresa.", "Poihana.", "Jiraira.", 0};

static int starts_with(const char* text, const char* start) {
    while (*start != 0) {
        if (*text++ != *start++) {
            return 0;
        }
    }
    return 1;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    int index = 0;
    const char* nerve;
    game = api;
    me = self;
    while ((nerve = game->nerve_at(index++)) != 0) {
        int i;
        for (i = 0; classes[i] != 0; ++i) {
            if (starts_with(nerve, classes[i])) {
                game->hook_nerve(nerve, hold, 0);
            }
        }
    }
}
