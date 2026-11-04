/* Makes every call of the stable interface and logs what came back. Nebula's test of itself, not an example to copy. */
#include <nebula_mod_api.h>

int snprintf(char* buffer, unsigned long long size, const char* format, ...);

static const NebulaModApi* game;
static const NebulaPlugin* me;
static int frames;
static int seen[NEBULA_MOD_EVENT_COUNT];
static NebulaActor* spawned;

static void say(const char* what, int a, int b, int c) {
    char line[160];
    snprintf(line, sizeof(line), "%s %d %d %d", what, a, b, c);
    game->log(me, line);
}

static int32_t any_event(const NebulaModEventData* data, void* user) {
    const int event = (int)(intptr_t)user;
    /* Counted from the start of each stage: the title's objects are gone by then. */
    if (event == NEBULA_MOD_EVENT_STAGE_START) {
        frames = 0;
        spawned = 0;
    }
    if (seen[event]++ < 2) {
        char line[160];
        snprintf(line, sizeof(line), "event %d value %d name %s", event, data->value, data->name != 0 ? data->name : "-");
        game->log(me, line);
    }
    return data->value;
}

static void object_start(NebulaActor* actor, void* user) {
    (void)actor;
    (void)user;
    game->log(me, "object start");
}

static void object_update(NebulaActor* actor, void* user) {
    static int n;
    float at[3];
    (void)user;
    game->get_position(actor, at);
    at[1] += 2.0f;
    game->set_position(actor, at);
    if (n++ == 0) {
        game->log(me, "object update");
    }
}

static void object_touch(NebulaActor* actor, void* user) {
    static int n;
    (void)actor;
    (void)user;
    if (n++ == 0) {
        game->log(me, "object touched the player");
    }
}

static int new_power;
static int power_frames;

static void power_start(void* user) {
    (void)user;
    power_frames = 0;
    game->log(me, "power-up start");
}

static void power_update(void* user) {
    (void)user;
    ++power_frames;
}

static void power_end(void* user) {
    (void)user;
    say("power-up end, frames", power_frames, game->value(NEBULA_VALUE_POWER_UP), 0);
}

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    NebulaActor* player = game->player();
    float at[3] = {0, 0, 0}, velocity[3] = {0, 0, 0}, x = 0, y = 0;
    (void)user;
    ++frames;
    if (player == 0) {
        if (frames == 1) {
            game->log(me, "no player in a stage frame");
        }
        return data->value;
    }
    if (frames == 300) {
        game->get_position(player, at);
        game->get_velocity(player, velocity);
        game->stick(&x, &y);
        say("player at", (int)at[0], (int)at[1], (int)at[2]);
        say("player velocity", (int)velocity[0], (int)velocity[1], (int)velocity[2]);
        say("life max power", game->value(NEBULA_VALUE_LIFE), game->value(NEBULA_VALUE_MAX_LIFE), game->value(NEBULA_VALUE_POWER_UP));
        say("ground bits coins", game->value(NEBULA_VALUE_ON_GROUND), game->value(NEBULA_VALUE_STAR_BITS), game->value(NEBULA_VALUE_COINS));
        say("lives mission buttons", game->value(NEBULA_VALUE_LIVES), game->value(NEBULA_VALUE_MISSION), (int)game->buttons_held());
        game->log(me, game->stage_name() != 0 ? game->stage_name() : "no stage");
        game->log(me, game->actor_name(player));
        game->log(me, game->option(me, "mode") != 0 ? game->option(me, "mode") : "no option");
    }
    if (frames == 320) {
        const int before = game->value(NEBULA_VALUE_STAR_BITS);
        game->set_value(NEBULA_SET_ADD_STAR_BITS, 3);
        say("star bits before after", before, game->value(NEBULA_VALUE_STAR_BITS), 0);
    }
    if (frames == 340) {
        const int before = game->value(NEBULA_VALUE_COINS);
        game->set_value(NEBULA_SET_ADD_COINS, 2);
        say("coins before after", before, game->value(NEBULA_VALUE_COINS), 0);
    }
    if (frames == 360) {
        const int before = game->value(NEBULA_VALUE_LIFE);
        game->set_value(NEBULA_SET_LIFE, 1);
        say("life before after", before, game->value(NEBULA_VALUE_LIFE), 0);
        game->set_value(NEBULA_SET_LIFE, before);
    }
    if (frames == 380) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_FIRE);
        say("power up", game->value(NEBULA_VALUE_POWER_UP), 0, 0);
    }
    if (frames == 400) {
        game->set_value(NEBULA_SET_POWER_UP, NEBULA_POWER_UP_NONE);
        game->get_position(player, at);
        at[0] += 300.0f;
        at[1] += 100.0f;
        spawned = game->spawn("NebulaSelfTest", at);
        say("spawned", spawned != 0, 0, 0);
    }
    if (frames == 420) {
        const float up[3] = {0, 30, 0};
        game->get_position(player, at);
        say("before jump y on ground", (int)at[1], game->value(NEBULA_VALUE_ON_GROUND), 0);
        game->jump_player(up);
        game->shake_camera();
    }
    if (frames == 440) {
        game->get_position(player, at);
        say("after jump y on ground", (int)at[1], game->value(NEBULA_VALUE_ON_GROUND), 0);
    }
    if (frames == 460 && spawned != 0) {
        game->play(spawned, NEBULA_PLAY_SOUND, "SE_OJ_SWITCH_1");
        game->play(spawned, NEBULA_PLAY_SOUND, "NOT_A_SOUND");
        game->play(spawned, NEBULA_PLAY_ANIMATION, "NotAnAnimation");
        game->play(spawned, NEBULA_PLAY_EFFECT, "NotAnEffect");
        game->log(me, "wrong names did nothing");
        game->show_model(spawned, 0);
        game->show_model(spawned, 1);
        game->get_position(spawned, at);
        say("object at", (int)at[0], (int)at[1], (int)at[2]);
        say("object dead", game->is_dead(spawned), 0, 0);
        game->kill(spawned);
        say("object dead", game->is_dead(spawned), 0, 0);
        game->appear(spawned);
        say("object dead", game->is_dead(spawned), 0, 0);
    }
    if (frames == 470) {
        NebulaActor* with_model;
        game->get_position(player, at);
        at[2] += 200.0f;
        with_model = game->spawn("NebulaSelfTestModel", at);
        say("spawned with a model", with_model != 0, 0, 0);
        if (with_model != 0) {
            say("the new particle played, a wrong name", game->play(with_model, NEBULA_PLAY_EFFECT, "NebulaTestFire"), game->play(with_model, NEBULA_PLAY_EFFECT, "NotAnEffect"), 0);
            game->play(with_model, NEBULA_PLAY_ANIMATION, "Wait");
        }
    }
    if (frames == 475) {
        game->set_value(NEBULA_SET_POWER_UP, new_power);
        say("the new power-up", game->value(NEBULA_VALUE_POWER_UP), new_power, 0);
    }
    if (frames == 480) {
        game->play(player, NEBULA_PLAY_PLAYER_ANIMATION, "Wait");
        game->log(me, "done");
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    static NebulaObjectClass object = {sizeof(NebulaObjectClass), "NebulaSelfTest", 0, 100.0f, 0, object_start, object_update, object_touch, 0};
    static NebulaObjectClass with_model = {sizeof(NebulaObjectClass), "NebulaSelfTestModel", "Coin", 100.0f, 0, object_start, 0, 0, 0};
    int event;
    game = api;
    me = self;
    for (event = 0; event < NEBULA_MOD_EVENT_COUNT; ++event) {
        if (game->subscribe((NebulaModEvent)event, any_event, (void*)(intptr_t)event) != 0) {
            game->log(me, "subscribe failed");
        }
    }
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
    {
        static const NebulaPowerUpClass power = {sizeof(NebulaPowerUpClass), "NebulaTestPower", 60, "BGM_FIRE_A", 1, 0, power_start, power_update, power_end};
        new_power = game->add_power_up(&power);
    }
    if (game->register_object(&object) != 0 || game->register_object(&with_model) != 0) {
        game->log(me, "register_object failed");
    }
    if (game->register_object(&object) == 0) {
        game->log(me, "the same name was taken twice");
    }
    if (game->subscribe(NEBULA_MOD_EVENT_COUNT, any_event, 0) == 0) {
        game->log(me, "a bad event was taken");
    }
    if (game->find_function("nebula_game_run") == 0) {
        game->log(me, "find_function failed");
    }
    game->log(me, "started");
}
