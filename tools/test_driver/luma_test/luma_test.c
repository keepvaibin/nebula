/* SPDX-License-Identifier: GPL-3.0-only */
/* The harness's game half (contract `harness` r1, section 2): a LumaCore mod that uses only what any mod can use
   (functions, fields and hooks by name) plus the harness service the test extension offers in a harness run
   (luma_harness.h). Once a game frame it asks the host half for its next lines and does each: the save, the stage, the
   player, calls and reads by name. Each is read back through the game's own query and reported as facts; one that does
   not take is answered as failed, naming it, so the test ends as setup-failed instead of proving nothing. */
#include <luma.h>
#include <luma_harness.h>

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static const LumaCore* core;
static const LumaMod* self;
static const LumaHarness* harness;

/* The game's functions, by name and signature as the game's source writes them. */
static const LumaFunction* change_stage;    /* void MR::requestChangeStageInGameMoving(const char *, s32) */
static const LumaFunction* stage_name;      /* const char *MR::getCurrentStageName() */
static const LumaFunction* scenario_no;     /* s32 MR::getCurrentScenarioNo() */
static const LumaFunction* opening_camera;  /* bool MR::isStageStateScenarioOpeningCamera() */
static const LumaFunction* scenario_start;  /* bool MR::isExecScenarioStarter() */
static const LumaFunction* scenario_parser; /* ScenarioDataParser *ScenarioDataFunction::getScenarioDataParser() */
static const LumaFunction* scenario_data;   /* const ScenarioData *ScenarioDataParser::getScenarioData(const char *) const */
static const LumaFunction* player_actor;    /* MarioActor *MarioAccess::getPlayerActor() */
static const LumaFunction* player_pos;      /* Vec3f *MR::getPlayerPos() */
static const LumaFunction* set_player_pos;  /* void MR::setPlayerPos(const Vec3f &) */
static const LumaFunction* player_gravity;  /* const Vec3f *MR::getPlayerGravity() */
static const LumaFunction* set_mode;        /* void MarioActor::setPlayerMode(u16, bool) */
static const LumaFunction* grant_spin;      /* void MR::onGameEventFlagEnableToSpinAndStarPointer() */
static const LumaFunction* set_swing;       /* void MR::setPlayerSwingPermission(bool) */
static const LumaFunction* passed_story;    /* bool GameDataFunction::isPassedStoryEvent(const char *) */
static const LumaFunction* flag_exists;     /* bool GameEventFlagTable::isExist(const char *) */
static const LumaFunction* flag_on;         /* void GameDataFunction::onGameEventFlag(const char *) */
static const LumaFunction* is_flag_on;      /* bool GameDataFunction::isOnGameEventFlag(const char *) */
static const LumaFunction* set_value;       /* void GameDataFunction::setGameEventValue(const char *, u16) */
static const LumaFunction* get_value;       /* u32 GameDataFunction::getGameEventValue(const char *) */
static const LumaFunction* star_count;      /* s32 GameDataFunction::calcCurrentPowerStarNum() */
static const LumaFunction* grand_count;     /* s32 GameDataFunction::calcGrandStarNum(const GameDataHolder *) */
static const LumaFunction* game_data;       /* GameDataHolder *GameDataFunction::getCurrentGameDataHolder() */
static const LumaFunction* is_luigi;        /* bool MR::isPlayerLuigi() */
static const LumaFunction* is_dead;         /* bool MR::isDead(const LiveActor *) */
static const LumaFunction* language_name;   /* const char *MR::getCurrentLanguagePrefix() */

static int32_t field_name = -1;      /* NameObj.mName */
static int32_t field_position = -1; /* LiveActor.mPosition */
static int32_t field_is_mario = -1; /* GameDataHolder.mIsMario */
static int32_t field_user = -1;     /* GameDataHolder.mUser */
static int32_t field_mode = -1;     /* MarioActor.mPlayerMode */
static int32_t field_health = -1;   /* MarioActor.mHealth */

/* --- Text ------------------------------------------------------------------------------------------------------------ */

static int same(const char* a, const char* b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static uint32_t length(const char* text) {
    uint32_t n = 0;
    while (text[n] != 0) {
        n++;
    }
    return n;
}

typedef struct Out {
    char text[1024];
    uint32_t at;
} Out;

static void put(Out* out, const char* text) {
    while (*text != 0 && out->at + 1 < sizeof(out->text)) {
        out->text[out->at++] = *text++;
    }
    out->text[out->at] = 0;
}

static void put_int(Out* out, int64_t value) {
    char digits[24];
    int32_t count = 0;
    uint64_t left = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    if (value < 0) {
        put(out, "-");
    }
    do {
        digits[count++] = (char)('0' + (int)(left % 10u));
        left /= 10u;
    } while (left != 0u);
    char one[2] = {0, 0};
    while (count > 0) {
        one[0] = digits[--count];
        put(out, one);
    }
}

/* A float as JSON, to 1/1000 (enough for positions in game units and for factors). */
static void put_float(Out* out, float value) {
    if (value != value) {
        put(out, "null");
        return;
    }
    double v = value;
    if (v < 0) {
        put(out, "-");
        v = -v;
    }
    const uint64_t whole = (uint64_t)v;
    uint64_t part = (uint64_t)((v - (double)whole) * 1000.0 + 0.5);
    uint64_t carry = 0;
    if (part >= 1000u) {
        part -= 1000u;
        carry = 1u;
    }
    put_int(out, (int64_t)(whole + carry));
    put(out, ".");
    char three[4] = {(char)('0' + (int)(part / 100u)), (char)('0' + (int)(part / 10u % 10u)), (char)('0' + (int)(part % 10u)), 0};
    put(out, three);
}

static void put_string(Out* out, const char* text) {
    put(out, "\"");
    for (; *text != 0; text++) {
        const char c = *text;
        if (c == '"' || c == '\\') {
            char two[3] = {'\\', c, 0};
            put(out, two);
        } else if ((unsigned char)c < 0x20u) {
            put(out, " ");
        } else {
            char one[2] = {c, 0};
            put(out, one);
        }
    }
    put(out, "\"");
}

static void put_vec(Out* out, const Vec3* v) {
    put(out, "[");
    put_float(out, v->x);
    put(out, ",");
    put_float(out, v->y);
    put(out, ",");
    put_float(out, v->z);
    put(out, "]");
}

/* Words of a plan line (kept in static storage where used: only the game thread runs the harness, and a frame
   over 4 KiB would need the stack probe a library with no C library lacks), split at spaces; the last word may hold spaces when `rest` is asked for. */
typedef struct Words {
    char buffer[1024];
    const char* word[16];
    uint32_t count;
} Words;

static void split(Words* w, const char* line, char separator) {
    uint32_t n = 0;
    w->count = 0;
    while (line[n] != 0 && n + 1 < sizeof(w->buffer)) {
        w->buffer[n] = line[n];
        n++;
    }
    w->buffer[n] = 0;
    char* at = w->buffer;
    while (*at != 0 && w->count < 16u) {
        w->word[w->count++] = at;
        while (*at != 0 && *at != separator) {
            at++;
        }
        if (*at == separator) {
            *at++ = 0;
        }
    }
}

static int64_t to_int(const char* text, int* good) {
    int64_t value = 0;
    int negative = 0;
    *good = 0;
    if (*text == '-') {
        negative = 1;
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
        *good = 1;
    }
    if (*text != 0) {
        *good = 0;
    }
    return negative ? -value : value;
}

static float to_float(const char* text, int* good) {
    double value = 0.0, scale = 1.0;
    int negative = 0, digits = 0;
    if (*text == '-') {
        negative = 1;
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10.0 + (*text++ - '0');
        digits++;
    }
    if (*text == '.') {
        text++;
        while (*text >= '0' && *text <= '9') {
            scale /= 10.0;
            value += (*text++ - '0') * scale;
            digits++;
        }
    }
    *good = digits > 0 && *text == 0;
    return (float)(negative ? -value : value);
}

/* --- Calls ----------------------------------------------------------------------------------------------------------- */

static void call0(const LumaFunction* f, void* result) {
    void* args[] = {0};
    f->invoke(args, result);
}

static void call1(const LumaFunction* f, void* a, void* result) {
    void* args[] = {0, a};
    f->invoke(args, result);
}

static int ask_text(const LumaFunction* f, const char* text) {
    unsigned char answer = 0;
    call1(f, (void*)&text, &answer);
    return answer != 0;
}

static void* player(void) {
    void* actor = 0;
    call0(player_actor, &actor);
    return actor;
}

static void fact(const char* key, Out* json) {
    harness->fact(key, json->text);
}

static void fact_bool(const char* key, int value) {
    harness->fact(key, value ? "true" : "false");
}

static void fact_int(const char* key, int64_t value) {
    Out o;
    o.at = 0;
    o.text[0] = 0;
    put_int(&o, value);
    fact(key, &o);
}

static void fact_text(const char* key, const char* value) {
    Out o;
    o.at = 0;
    o.text[0] = 0;
    put_string(&o, value);
    fact(key, &o);
}

/* --- Objects by name ------------------------------------------------------------------------------------------------- */

#define MOST_OBJECTS 4096
static void* objects[MOST_OBJECTS];
static uint32_t object_count;
static uint32_t objects_lost;

static int32_t on_object(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (object_count < MOST_OBJECTS) {
        objects[object_count++] = LUMA_THIS(call, void);
    } else {
        objects_lost++;
    }
    return 0;
}

/* The n-th live object (creation order) whose name is `name`, or null. */
static void* object_named(const char* name, int64_t n) {
    for (uint32_t i = 0; i < object_count; i++) {
        const char* const its = *(const char* const*)((const char*)objects[i] + field_name);
        unsigned char dead = 1;
        if (its == 0 || !same(its, name)) {
            continue;
        }
        call1(is_dead, &objects[i], &dead);
        if (dead == 0 && n-- == 0) {
            return objects[i];
        }
    }
    return 0;
}

/* "player" or "object:<name>#<n>". */
static void* object_of(const char* ref) {
    if (same(ref, "player")) {
        return player();
    }
    const char prefix[] = "object:";
    for (uint32_t i = 0; i + 1 < sizeof(prefix); i++) {
        if (ref[i] != prefix[i]) {
            return 0;
        }
    }
    static Words w;
    split(&w, ref + 7, '#');
    int good = 0;
    const int64_t n = w.count == 2u ? to_int(w.word[1], &good) : 0;
    return w.count == 2u && good ? object_named(w.word[0], n) : 0;
}

/* --- What a line asks ------------------------------------------------------------------------------------------------ */

typedef enum Pending { Pending_None, Pending_Stage } Pending;
static Pending pending;
static char pending_stage[64];
static int32_t pending_scenario;
static uint32_t pending_frames;
static int pending_is_step;
static int32_t frame_now = -1;

static const char* const kForms[] = {"normal", "rainbow", "fire", "ice", "bee", "spring", "boo", "flying", 0, "tornado"};

static int stage_ready(void) {
    const char* name = 0;
    int32_t scenario = 0;
    unsigned char opening = 1, starting = 1;
    call0(stage_name, &name);
    call0(scenario_no, &scenario);
    call0(opening_camera, &opening);
    call0(scenario_start, &starting);
    return name != 0 && same(name, pending_stage) && scenario == pending_scenario && opening == 0 && starting == 0 && player() != 0;
}

static void start_stage(const char* name, const char* scenario_text, int is_step) {
    int good = 0;
    const int64_t scenario = to_int(scenario_text, &good);
    void* parser = 0;
    const void* data = 0;
    call0(scenario_parser, &parser);
    if (parser != 0) {
        void* args[] = {parser, (void*)&name};
        scenario_data->invoke(args, &data);
    }
    if (!good || data == 0 || length(name) + 1u > sizeof(pending_stage)) {
        harness->done(0, "the game has no stage of that name");
        return;
    }
    for (uint32_t i = 0; i <= length(name); i++) {
        pending_stage[i] = name[i];
    }
    pending_scenario = (int32_t)scenario;
    pending_frames = 0;
    pending_is_step = is_step;
    pending = Pending_Stage;
    object_count = 0;
    objects_lost = 0;
    const char* stage = pending_stage;
    void* args[] = {0, (void*)&stage, &pending_scenario};
    change_stage->invoke(args, 0);
}

static void finish_stage(void) {
    Out o;
    o.at = 0;
    o.text[0] = 0;
    put(&o, "{\"name\":");
    put_string(&o, pending_stage);
    put(&o, ",\"scenario\":");
    put_int(&o, pending_scenario);
    put(&o, ",\"frame\":");
    put_int(&o, frame_now);
    put(&o, "}");
    fact(pending_is_step ? "stage.ready" : "setup.stage", &o);
    if (!pending_is_step) {
        fact("stage.ready", &o);
    }
    pending = Pending_None;
    harness->done(1, 0);
}

static void setup_save(const Words* w) {
    const char* const item = w->word[1];
    int good = 0;
    if (same(item, "save.character") && w->count == 3u) {
        void* holder = 0;
        call0(game_data, &holder);
        if (holder == 0 || field_is_mario < 0 || field_user < 0) {
            harness->done(0, field_is_mario < 0 ? "not built yet: GameDataHolder has no fields by name (task 1.2)" : "save.character: no game data to set");
            return;
        }
        *(unsigned char*)((char*)holder + field_is_mario) = same(w->word[2], "luigi") ? 0u : 1u;
        *(int32_t*)((char*)holder + field_user) = 1;
        const int mario = *(const unsigned char*)((const char*)holder + field_is_mario) != 0;
        fact_text("setup.save.character", mario ? "mario" : "luigi");
        harness->done(same(w->word[2], mario ? "mario" : "luigi"), "save.character did not take");
        return;
    }
    if (same(item, "save.spin") && w->count == 3u) {
        const int want = same(w->word[2], "1");
        if (want) {
            void* none = 0;
            call0(grant_spin, &none);
            unsigned char on = 1;
            call1(set_swing, &on, 0);
        }
        const int passed = ask_text(passed_story, "\xe3\x82\xb9\xe3\x83\x94\xe3\x83\xb3\xe6\xa8\xa9\xe5\x88\xa9"); /* スピン権利 */
        fact_bool("setup.save.spin", passed);
        harness->done(passed == want, "save.spin did not take");
        return;
    }
    if (same(item, "save.flag") && w->count == 3u) {
        if (!ask_text(flag_exists, w->word[2])) {
            harness->done(0, "save.flags: the game has no event flag of that name");
            return;
        }
        call1(flag_on, (void*)&w->word[2], 0);
        const int on = ask_text(is_flag_on, w->word[2]);
        Out key;
    key.at = 0;
    key.text[0] = 0;
        put(&key, "setup.save.flags.");
        put(&key, w->word[2]);
        fact_bool(key.text, on);
        harness->done(on, "save.flags did not take");
        return;
    }
    if (same(item, "save.value") && w->count == 4u) {
        const int64_t value = to_int(w->word[3], &good);
        if (!good || value < 0 || value > 65535) {
            harness->done(0, "save.values: a value is 0 to 65535");
            return;
        }
        uint16_t v16 = (uint16_t)value;
        void* args[] = {0, (void*)&w->word[2], &v16};
        set_value->invoke(args, 0);
        uint32_t back = 0;
        call1(get_value, (void*)&w->word[2], &back);
        Out key;
    key.at = 0;
    key.text[0] = 0;
        put(&key, "setup.save.values.");
        put(&key, w->word[2]);
        fact_int(key.text, back);
        harness->done(back == (uint32_t)value, "save.values did not take");
        return;
    }
    if ((same(item, "save.stars") || same(item, "save.grand_stars")) && w->count == 3u) {
        const int64_t want = to_int(w->word[2], &good);
        int32_t have = -1;
        if (same(item, "save.stars")) {
            call0(star_count, &have);
        } else {
            void* holder = 0;
            call0(game_data, &holder);
            call1(grand_count, &holder, &have);
        }
        fact_int(same(item, "save.stars") ? "setup.save.stars" : "setup.save.grand_stars", have);
        if (good && want != have) {
            harness->done(0, "not built yet: setting a number of stars other than the file's own");
            return;
        }
        harness->done(good, "a number of stars is a whole number");
        return;
    }
    harness->done(0, "not a save item the harness knows");
}

static void setup_player(const Words* w) {
    const char* const item = w->word[1];
    void* const actor = player();
    int good = 1;
    if (actor == 0) {
        harness->done(0, "player: there is no player (no stage)");
        return;
    }
    if ((same(item, "player.position") && w->count == 5u) || (same(item, "player.at") && w->count == 4u)) {
        Vec3 place;
        if (same(item, "player.position")) {
            int g1 = 0, g2 = 0, g3 = 0;
            place.x = to_float(w->word[2], &g1);
            place.y = to_float(w->word[3], &g2);
            place.z = to_float(w->word[4], &g3);
            good = g1 && g2 && g3;
        } else {
            const int64_t n = to_int(w->word[3], &good);
            void* const object = good ? object_named(w->word[2], n) : 0;
            if (object == 0) {
                harness->done(0, objects_lost != 0 ? "player.at: more objects than the harness keeps" : "player.at: no live object of that name and number");
                return;
            }
            const Vec3* const at = (const Vec3*)((const char*)object + field_position);
            const Vec3* down = 0;
            call0(player_gravity, &down);
            const Vec3 g = down != 0 ? *down : (Vec3){0.0f, -1.0f, 0.0f};
            place.x = at->x - g.x * 150.0f;
            place.y = at->y - g.y * 150.0f;
            place.z = at->z - g.z * 150.0f;
        }
        if (!good) {
            harness->done(0, "player.position is three numbers");
            return;
        }
        Vec3* p = &place;
        call1(set_player_pos, &p, 0);
        Vec3* back = 0;
        call0(player_pos, &back);
        Out o;
    o.at = 0;
    o.text[0] = 0;
        if (back != 0) {
            put_vec(&o, back);
        } else {
            put(&o, "null");
        }
        fact(same(item, "player.position") ? "setup.player.position" : "setup.player.at", &o);
        const float dx = back != 0 ? back->x - place.x : 1e9f, dy = back != 0 ? back->y - place.y : 0.0f, dz = back != 0 ? back->z - place.z : 0.0f;
        harness->done(dx * dx + dy * dy + dz * dz < 1.0f, "the player is not where he was put");
        return;
    }
    if (same(item, "player.form") && w->count == 3u) {
        int32_t mode = -1;
        for (int32_t i = 0; i < (int32_t)(sizeof(kForms) / sizeof(kForms[0])); i++) {
            if (kForms[i] != 0 && same(kForms[i], w->word[2])) {
                mode = i;
            }
        }
        if (mode < 0 || field_mode < 0) {
            harness->done(0, "player.form: the game has no form of that name");
            return;
        }
        uint16_t m = (uint16_t)mode;
        unsigned char morph = 1;
        void* args[] = {actor, &m, &morph};
        set_mode->invoke(args, 0);
        const uint16_t back = *(const uint16_t*)((const char*)actor + field_mode);
        fact_text("setup.player.form", back < sizeof(kForms) / sizeof(kForms[0]) && kForms[back] != 0 ? kForms[back] : "?");
        harness->done(back == m, "player.form did not take");
        return;
    }
    if (same(item, "player.life") && w->count == 3u) {
        const int64_t life = to_int(w->word[2], &good);
        if (!good || life < 1 || field_health < 0) {
            harness->done(0, "player.life is 1 or more");
            return;
        }
        *(uint32_t*)((char*)actor + field_health) = (uint32_t)life;
        const uint32_t back = *(const uint32_t*)((const char*)actor + field_health);
        fact_int("setup.player.life", back);
        harness->done(back == (uint32_t)life, "player.life did not take");
        return;
    }
    harness->done(0, "not a player item the harness knows");
}

/* `call <fact or -> <name>\t<signature>\t<argument>\t...`: arguments as the plan writes them (numbers, true, false,
   strings, "x y z" vectors, player, object:<name>#<n>), each converted to the parameter's type from the signature. */
#define MOST_ARGS 8
static void do_call(const char* key, const char* rest) {
    static Words parts;
    split(&parts, rest, '\t');
    if (parts.count < 2u) {
        harness->done(0, "a call needs a name and a signature");
        return;
    }
    const LumaFunction* const f = core->function(parts.word[0], parts.word[1]);
    if (f == 0) {
        harness->done(0, "the game has no function of that name and signature");
        return;
    }
    /* The parameter types, between the signature's outer parentheses. */
    const char* sig = parts.word[1];
    const char* open = sig;
    while (*open != 0 && *open != '(') {
        open++;
    }
    char params[256];
    uint32_t n = 0;
    if (*open == '(') {
        open++;
        while (open[n] != 0 && open[n] != ')' && n + 1 < sizeof(params)) {
            params[n] = open[n];
            n++;
        }
    }
    params[n] = 0;
    static Words types;
    split(&types, params, ',');
    if (n == 0u) {
        types.count = 0;
    }
    if (types.count != parts.count - 2u || types.count > MOST_ARGS) {
        harness->done(0, "the call's arguments do not match its signature");
        return;
    }
    union Value {
        float f;
        int32_t i;
        uint32_t u;
        uint16_t h;
        unsigned char b;
        const char* s;
        void* p;
        Vec3 v;
    } values[MOST_ARGS];
    Vec3* vec_pointers[MOST_ARGS];
    void* args[MOST_ARGS + 1];
    args[0] = 0;
    for (uint32_t i = 0; i < types.count; i++) {
        const char* type = types.word[i];
        while (*type == ' ') {
            type++;
        }
        const char* const text = parts.word[2 + i];
        int good = 1;
        if (same(type, "f32") || same(type, "float")) {
            values[i].f = to_float(text, &good);
            args[i + 1] = &values[i].f;
        } else if (same(type, "s32") || same(type, "int")) {
            values[i].i = (int32_t)to_int(text, &good);
            args[i + 1] = &values[i].i;
        } else if (same(type, "u32")) {
            values[i].u = (uint32_t)to_int(text, &good);
            args[i + 1] = &values[i].u;
        } else if (same(type, "u16")) {
            values[i].h = (uint16_t)to_int(text, &good);
            args[i + 1] = &values[i].h;
        } else if (same(type, "bool")) {
            good = same(text, "true") || same(text, "false");
            values[i].b = same(text, "true") ? 1u : 0u;
            args[i + 1] = &values[i].b;
        } else if (same(type, "const char *")) {
            values[i].s = text;
            args[i + 1] = &values[i].s;
        } else if (same(type, "const Vec3f &") || same(type, "const TVec3f &") || same(type, "Vec3f *") || same(type, "const Vec3f *") ||
                   same(type, "Vec3f") || same(type, "TVec3f")) {
            static Words xyz;
            split(&xyz, text, ' ');
            int g1 = 0, g2 = 0, g3 = 0;
            good = xyz.count == 3u;
            values[i].v.x = good ? to_float(xyz.word[0], &g1) : 0.0f;
            values[i].v.y = good ? to_float(xyz.word[1], &g2) : 0.0f;
            values[i].v.z = good ? to_float(xyz.word[2], &g3) : 0.0f;
            good = good && g1 && g2 && g3;
            vec_pointers[i] = &values[i].v;
            args[i + 1] = (same(type, "Vec3f") || same(type, "TVec3f")) ? (void*)&values[i].v : (void*)&vec_pointers[i];
        } else if (same(type, "LiveActor *") || same(type, "const LiveActor *") || same(type, "NameObj *") || same(type, "const NameObj *") ||
                   same(type, "MarioActor *")) {
            values[i].p = object_of(text);
            good = values[i].p != 0;
            args[i + 1] = &values[i].p;
        } else {
            harness->done(0, "a parameter type the harness cannot pass");
            return;
        }
        if (!good) {
            harness->done(0, "an argument does not fit its parameter, or names no live object");
            return;
        }
    }
    /* The result, by the signature's return type (the text before the parentheses). */
    char ret[64];
    uint32_t r = 0;
    while (sig[r] != 0 && sig[r] != '(' && r + 1 < sizeof(ret)) {
        ret[r] = sig[r];
        r++;
    }
    while (r > 0 && ret[r - 1] == ' ') {
        r--;
    }
    ret[r] = 0;
    union Value result;
    result.v.x = result.v.y = result.v.z = 0.0f;
    f->invoke(args, same(ret, "void") ? (void*)0 : (void*)&result);
    if (same(key, "-")) {
        harness->done(1, 0);
        return;
    }
    Out o;
    o.at = 0;
    o.text[0] = 0;
    if (same(ret, "void")) {
        put(&o, "null");
    } else if (same(ret, "f32") || same(ret, "float")) {
        put_float(&o, result.f);
    } else if (same(ret, "s32") || same(ret, "int")) {
        put_int(&o, result.i);
    } else if (same(ret, "u32")) {
        put_int(&o, result.u);
    } else if (same(ret, "u16")) {
        put_int(&o, result.h);
    } else if (same(ret, "bool")) {
        put(&o, result.b != 0 ? "true" : "false");
    } else if (same(ret, "const char *")) {
        if (result.s != 0) {
            put_string(&o, result.s);
        } else {
            put(&o, "null");
        }
    } else if (same(ret, "Vec3f *") || same(ret, "const Vec3f *") || same(ret, "TVec3f *") || same(ret, "const TVec3f *")) {
        if (result.p != 0) {
            put_vec(&o, (const Vec3*)result.p);
        } else {
            put(&o, "null");
        }
    } else if (same(ret, "Vec3f") || same(ret, "TVec3f")) {
        put_vec(&o, &result.v);
    } else {
        /* Any other pointer: whether there is one. */
        put(&o, result.p != 0 ? "true" : "false");
    }
    fact(key, &o);
    harness->done(1, 0);
}

/* `read <fact> <of> <Class.field> <type>`. */
static void do_read(const Words* w) {
    if (w->count < 5u) {
        harness->done(0, "a read needs a fact, an object, a field and a type");
        return;
    }
    static Words cf;
    split(&cf, w->word[3], '.');
    /* The type may hold spaces: the rest of the line. */
    char type[64];
    uint32_t t = 0;
    for (uint32_t i = 4; i < w->count; i++) {
        const char* part = w->word[i];
        if (i > 4) {
            type[t++] = ' ';
        }
        while (*part != 0 && t + 2 < sizeof(type)) {
            type[t++] = *part++;
        }
    }
    type[t] = 0;
    void* const object = object_of(w->word[2]);
    const int32_t offset = cf.count == 2u ? core->field(cf.word[0], cf.word[1], type) : -1;
    if (object == 0 || offset < 0) {
        harness->done(0, object == 0 ? "no live object of that name and number" : "the game has no field of that name and type");
        return;
    }
    const char* const at = (const char*)object + offset;
    Out o;
    o.at = 0;
    o.text[0] = 0;
    if (same(type, "f32")) {
        put_float(&o, *(const float*)at);
    } else if (same(type, "s32")) {
        put_int(&o, *(const int32_t*)at);
    } else if (same(type, "u32")) {
        put_int(&o, *(const uint32_t*)at);
    } else if (same(type, "u16")) {
        put_int(&o, *(const uint16_t*)at);
    } else if (same(type, "s16")) {
        put_int(&o, *(const int16_t*)at);
    } else if (same(type, "u8")) {
        put_int(&o, *(const unsigned char*)at);
    } else if (same(type, "bool")) {
        put(&o, *(const unsigned char*)at != 0 ? "true" : "false");
    } else if (same(type, "TVec3f") || same(type, "Vec3f")) {
        put_vec(&o, (const Vec3*)at);
    } else if (same(type, "const char *")) {
        const char* const s = *(const char* const*)at;
        if (s != 0) {
            put_string(&o, s);
        } else {
            put(&o, "null");
        }
    } else {
        harness->done(0, "a field type the harness cannot read");
        return;
    }
    fact(w->word[1], &o);
    harness->done(1, 0);
}

static void do_line(const char* line) {
    static Words w;
    split(&w, line, ' ');
    if (w.count == 0u) {
        harness->done(0, "an empty line");
        return;
    }
    if (same(w.word[0], "frame") && w.count == 2u) {
        int good = 0;
        frame_now = (int32_t)to_int(w.word[1], &good);
        harness->done(good, "a frame number");
        return;
    }
    if (same(w.word[0], "setup") && w.count >= 2u) {
        const char* const item = w.word[1];
        if (same(item, "stage") && w.count == 4u) {
            start_stage(w.word[2], w.word[3], 0);
        } else if (item[0] == 's' && item[1] == 'a' && item[2] == 'v' && item[3] == 'e' && item[4] == '.') {
            setup_save(&w);
        } else if (item[0] == 'p' && item[1] == 'l' && item[2] == 'a' && item[3] == 'y' && item[4] == 'e' && item[5] == 'r' && item[6] == '.') {
            setup_player(&w);
        } else if (same(item, "seed") && w.count == 3u) {
            /* The game seeds its generator from the stage's name as each stage starts: seed 0 is that. */
            fact_int("setup.seed", 0);
            harness->done(same(w.word[2], "0"), "not built yet: a seed other than 0 needs the game's globals by name (task 1.2)");
        } else if (same(item, "language") && w.count == 3u) {
            /* The runner set the language as the setting is set; the game's own name for the language it took. */
            const char* name = 0;
            call0(language_name, &name);
            fact_text("setup.language", name != 0 ? name : "");
            harness->done(name != 0 && same(name, w.word[2]), "language did not take (setup.language holds the one the game took)");
        } else if (same(item, "done")) {
            fact_bool("setup.done", 1);
            harness->done(1, 0);
        } else {
            harness->done(0, "not a setup item the harness knows");
        }
        return;
    }
    if (same(w.word[0], "stage") && w.count == 3u) {
        start_stage(w.word[1], w.word[2], 1);
        return;
    }
    if (same(w.word[0], "call") && w.count >= 3u) {
        /* The rest after "call <fact> ": tab-separated. */
        const char* rest = line + 5 + length(w.word[1]) + 1;
        do_call(w.word[1], rest);
        return;
    }
    if (same(w.word[0], "read")) {
        do_read(&w);
        return;
    }
    harness->done(0, "not a line the game half knows");
}

static int32_t on_frame(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (pending == Pending_Stage) {
        if (stage_ready()) {
            finish_stage();
        } else if (++pending_frames > 60u * 120u) {
            pending = Pending_None;
            harness->done(0, "the stage was not ready within 7,200 frames");
        } else {
            return 0;
        }
    }
    for (uint32_t guard = 0; guard < 256u && pending == Pending_None; guard++) {
        const char* const line = harness->next();
        if (line == 0) {
            break;
        }
        do_line(line);
    }
    return 0;
}

static int32_t start(const LumaCore* given, const LumaMod* mod) {
    core = given;
    self = mod;
    harness = (const LumaHarness*)core->service(LUMA_HARNESS_NAME, LUMA_HARNESS_MAJOR, 0);
    if (harness == 0 || harness->size < sizeof(LumaHarness)) {
        core->log(mod, "not in a harness run: the harness service is absent");
        return 1;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(change_stage, "MR::requestChangeStageInGameMoving", "void(const char *, s32)"),
    LUMA_FUNCTION(stage_name, "MR::getCurrentStageName", "const char *()"),
    LUMA_FUNCTION(scenario_no, "MR::getCurrentScenarioNo", "s32()"),
    LUMA_FUNCTION(opening_camera, "MR::isStageStateScenarioOpeningCamera", "bool()"),
    LUMA_FUNCTION(scenario_start, "MR::isExecScenarioStarter", "bool()"),
    LUMA_FUNCTION(scenario_parser, "ScenarioDataFunction::getScenarioDataParser", "ScenarioDataParser *()"),
    LUMA_FUNCTION(scenario_data, "ScenarioDataParser::getScenarioData", "const ScenarioData *(const char *) const"),
    LUMA_FUNCTION(player_actor, "MarioAccess::getPlayerActor", "MarioActor *()"),
    LUMA_FUNCTION(player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(set_player_pos, "MR::setPlayerPos", "void(const Vec3f &)"),
    LUMA_FUNCTION(player_gravity, "MR::getPlayerGravity", "const Vec3f *()"),
    LUMA_FUNCTION(set_mode, "MarioActor::setPlayerMode", "void(u16, bool)"),
    LUMA_FUNCTION(grant_spin, "MR::onGameEventFlagEnableToSpinAndStarPointer", "void()"),
    LUMA_FUNCTION(set_swing, "MR::setPlayerSwingPermission", "void(bool)"),
    LUMA_FUNCTION(passed_story, "GameDataFunction::isPassedStoryEvent", "bool(const char *)"),
    LUMA_FUNCTION(flag_exists, "GameEventFlagTable::isExist", "bool(const char *)"),
    LUMA_FUNCTION(flag_on, "GameDataFunction::onGameEventFlag", "void(const char *)"),
    LUMA_FUNCTION(is_flag_on, "GameDataFunction::isOnGameEventFlag", "bool(const char *)"),
    LUMA_FUNCTION(set_value, "GameDataFunction::setGameEventValue", "void(const char *, u16)"),
    LUMA_FUNCTION(get_value, "GameDataFunction::getGameEventValue", "u32(const char *)"),
    LUMA_FUNCTION(star_count, "GameDataFunction::calcCurrentPowerStarNum", "s32()"),
    LUMA_FUNCTION(grand_count, "GameDataFunction::calcGrandStarNum", "s32(const GameDataHolder *)"),
    LUMA_FUNCTION(game_data, "GameDataFunction::getCurrentGameDataHolder", "GameDataHolder *()"),
    LUMA_FUNCTION(is_luigi, "MR::isPlayerLuigi", "bool()"),
    LUMA_FUNCTION(is_dead, "MR::isDead", "bool(const LiveActor *)"),
    LUMA_FUNCTION(language_name, "MR::getCurrentLanguagePrefix", "const char *()"),
    LUMA_FIELD(field_name, "NameObj.mName", "const char *"),
    LUMA_FIELD(field_position, "LiveActor.mPosition", "TVec3f"),
    /* Optional: a class outside the game's object tree may have no fields by name yet (census D3, task 1.2); the item
       that needs one then fails, naming it. */
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "GameDataHolder.mIsMario", "bool", &field_is_mario, 0, 0, 0},
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "GameDataHolder.mUser", "s32", &field_user, 0, 0, 0},
    LUMA_FIELD(field_mode, "MarioActor.mPlayerMode", "u16"),
    LUMA_FIELD(field_health, "MarioActor.mHealth", "u32"),
    LUMA_HOOK_ON("LiveActor::LiveActor", "(const char *)", LUMA_AFTER, on_object),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, on_frame),
};

LUMA_MOD(imports, start, 0);
