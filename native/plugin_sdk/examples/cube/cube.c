/* A new object: a level places NebulaCube, and its model is the mod's own. It says when it is touched. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static void start(NebulaActor* actor, void* user) {
    (void)actor;
    (void)user;
    game->log(me, "the cube is here");
}

/* It floats over the player's head. */
static void update(NebulaActor* actor, void* user) {
    NebulaActor* player = game->player();
    float at[3], down[3];
    (void)user;
    if (player != 0) {
        game->get_position(player, at);
        game->get_gravity(player, down);
        at[0] -= down[0] * 400.0f;
        at[1] -= down[1] * 400.0f;
        at[2] -= down[2] * 400.0f;
        game->set_position(actor, at);
    }
}

static void touch_player(NebulaActor* actor, void* user) {
    static int said;
    (void)actor;
    (void)user;
    if (!said) {
        said = 1;
        game->log(me, "the cube was touched");
    }
}

/* It stands to one side of the player, swinging: an animation from its glTF. */
static void bar_start(NebulaActor* actor, void* user) {
    (void)user;
    game->log(me, game->play(actor, NEBULA_PLAY_ANIMATION, "Swing") != 0 ? "the bar swings" : "the bar can't swing");
}

static void bar_update(NebulaActor* actor, void* user) {
    NebulaActor* player = game->player();
    float at[3], down[3];
    (void)user;
    if (player != 0) {
        game->get_position(player, at);
        game->get_gravity(player, down);
        /* Any direction square to down. */
        at[0] += -down[1] * 350.0f;
        at[1] += down[0] * 350.0f;
        game->set_position(actor, at);
    }
}

NEBULA_PLUGIN_ENTRY(api, self) {
    static const NebulaObjectClass cube = {sizeof(NebulaObjectClass), "NebulaCube", "NebulaCube", 150.0f, 0, start, update, touch_player, 0};
    game = api;
    me = self;
    {
        static const NebulaObjectClass bar = {sizeof(NebulaObjectClass), "NebulaBar", "NebulaBar", 150.0f, 0, bar_start, bar_update, 0, 0};
        game->register_object(&bar);
    }
    if (game->register_object(&cube) != 0) {
        game->log(me, "the cube can't be registered");
    }
}
