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

/* Every nerve hooked: how often each phase ran, and whether a hook saw its own nerve's name. */
static int nerve_phases[3];
static int nerve_names_right = 1;

static int same_text(const char* a, const char* b) {
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int32_t any_nerve(NebulaActor* actor, const char* nerve, NebulaNervePhase phase, int32_t frames, void* user) {
    (void)actor;
    if (phase >= NEBULA_NERVE_ENTER && phase <= NEBULA_NERVE_LEAVE) {
        ++nerve_phases[phase];
    }
    if (phase == NEBULA_NERVE_ENTER && frames != 0) {
        nerve_names_right = 0;
    }
    if (nerve == 0 || (user != 0 && !same_text(nerve, (const char*)user))) {
        nerve_names_right = 0;
    }
    return 1;
}

/* The game's rules as popular Gecko codes change them. */
static int refuse_miss;
static int misses_refused;
static int speed_from;

static int32_t player_miss(const NebulaModEventData* data, void* user) {
    (void)user;
    if (refuse_miss) {
        ++misses_refused;
        return -1;
    }
    return data->value;
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
        {
            /* A spawned object has no row in the level: no column of any name. */
            double arg = 5.0;
            say("a spawned object's placement (0 5 1)", game->placement_number(spawned, "Obj_arg0", &arg), (int)arg,
                game->placement_text(spawned, "name") == 0);
        }
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
    if (frames == 478) {
        /* Nerves by name: the player's, set again to itself; one of another class refused; every hook phase ran. */
        const char* own = game->nerve(player);
        game->log(me, own != 0 ? own : "the player has no nerve");
        if (own != 0) {
            say("set the player's own nerve (0), another class's (-1)", game->set_nerve(player, own),
                game->set_nerve(player, "Kuribo.Walk"), game->nerve_frames(player) >= 0);
        }
        say("nerve enters frames leaves", nerve_phases[NEBULA_NERVE_ENTER] > 0, nerve_phases[NEBULA_NERVE_FRAME] > 0,
            nerve_phases[NEBULA_NERVE_LEAVE] > 0);
        say("hooks saw their own nerves", nerve_names_right, 0, 0);
    }
    if (frames == 480) {
        game->play(player, NEBULA_PLAY_PLAYER_ANIMATION, "Wait");
    }
    if (frames == 482) {
        game->set_gravity_scale(0.5f);
        say("gravity scale x100 (50), back (100)", (int)(game->gravity_scale() * 100.0f + 0.5f), 0, 0);
        game->set_gravity_scale(1.0f);
        say("gravity scale x100 (50), back (100)", (int)(game->gravity_scale() * 100.0f + 0.5f), 0, 0);
    }
    if (frames == 484) {
        float left = -1.0f, limit = -1.0f;
        say("no countdown here: timer (0), set (-1)", game->timer(&left, &limit), game->set_timer(10.0f), 0);
        game->pause_timer(1);
        game->pause_timer(0);
    }
    if (frames == 486) {
        float target[3], place[3];
        game->get_position(player, target);
        place[0] = target[0];
        place[1] = target[1] + 1500.0f;
        place[2] = target[2] + 1500.0f;
        game->set_camera(place, target, 70.0f);
    }
    if (frames == 490) {
        float seen[3];
        game->camera_position(seen);
        game->get_position(player, at);
        say("a plugin's camera: fov, height over the player (about 1500)", (int)(game->camera_fov() + 0.5f), (int)(seen[1] - at[1]), 0);
        game->set_camera(0, 0, 0.0f);
    }
    if (frames == 494) {
        say("the game's camera again: fov is not 70", (int)(game->camera_fov() + 0.5f) != 70, 0, 0);
    }
    if (frames == 500) {
        speed_from = game->nerve_frames(player);
        game->set_game_speed(0.5f);
    }
    if (frames == 520) {
        const int now = game->nerve_frames(player);
        say("half speed: steps in 20 frames (10)", now - speed_from, 0, 0);
        speed_from = now;
        game->set_game_speed(0.0f);
    }
    if (frames == 530) {
        const int now = game->nerve_frames(player);
        say("held: steps in 10 frames (0)", now - speed_from, 0, 0);
        speed_from = now;
        game->step_game(3);
    }
    if (frames == 540) {
        const int now = game->nerve_frames(player);
        say("held, 3 asked: steps in 10 frames (3)", now - speed_from, 0, 0);
        game->set_game_speed(1.0f);
    }
    if (frames == 550) {
        refuse_miss = 1;
        game->kill_player();
        refuse_miss = 0;
    }
    if (frames == 560) {
        say("a miss refused: refused, dead (1 0)", misses_refused, game->value(NEBULA_VALUE_DEAD), 0);
    }
    if (frames == 600) {
        /* Shown through the next pictures (GALAXY_PRESENT_SNAPSHOT_EVERY): no HUD, two lines of text. */
        game->show_hud(NEBULA_HUD_ALL, 0);
        say("text lines: set (0), past the last (-1)",
            game->draw_text(0, 0.04f, 0.06f, 0.05f, 0xFFFFFFFFu, "Nebula self test: no HUD"),
            game->draw_text(16, 0.0f, 0.0f, 0.05f, 0xFFFFFFFFu, "x"), 0);
        game->draw_text(1, 0.04f, 0.13f, 0.035f, 0xFFD040FFu, "Second line \xE2\x98\x85");
    }
    if (frames == 1500) {
        game->show_hud(NEBULA_HUD_ALL, 1);
        game->draw_text(0, 0, 0, 0, 0, 0);
        game->draw_text(1, 0, 0, 0, 0, "");
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
    game->subscribe(NEBULA_MOD_EVENT_PLAYER_MISS, player_miss, 0);
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
    {
        /* Every nerve the game has, each hooked with its own name as `user`. */
        int count = 0;
        int hooked = 0;
        const char* nerve;
        while ((nerve = game->nerve_at(count)) != 0) {
            hooked += game->hook_nerve(nerve, any_nerve, (void*)nerve) == 0;
            ++count;
        }
        say("nerves hooked (3630 3630), a wrong name (-1)", count, hooked, game->hook_nerve("NotA.Nerve", any_nerve, 0));
    }
    if (game->find_function("nebula_game_run") == 0) {
        game->log(me, "find_function failed");
    }
    game->log(me, "started");
}
