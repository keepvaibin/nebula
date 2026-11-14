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
static const LumaFunction* input_disabled;  /* bool MarioModule::isInputDisable() const */
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
static const LumaFunction* demo_active;     /* bool MR::isDemoActive() */
static const LumaFunction* save_sequence;   /* SaveDataHandleSequence *getSaveDataHandleSequence() (file-local) */
static const LumaFunction* current_file;    /* UserFile *SaveDataHandleSequence::getCurrentUserFile() */
static const LumaFunction* file_created;    /* bool UserFile::isCreated() const */
static const LumaFunction* load_file;       /* void GameSequenceFunction::startGameDataLoadSequence(int, bool) */
static const LumaFunction* create_file;     /* void GameSequenceFunction::startCreateUserFileSequence(int) */
static const LumaFunction* save_busy;       /* bool GameSequenceFunction::isActiveSaveDataHandleSequence() */
static const LumaFunction* save_success;    /* bool GameSequenceFunction::isSuccessSaveDataHandleSequence() */
static const LumaFunction* delete_file;     /* void GameSequenceFunction::startDeleteUserFileSequence(int) */
static const LumaFunction* copy_file;       /* void GameSequenceFunction::startCopyUserFileSequence(int to, int from) */
static const LumaFunction* store_file;      /* void SaveDataHandler::storeUserFile(const UserFile *) (optional: file copy) */
static const LumaFunction* arena_of;        /* NebulaArena *NebulaArena::findFromRoot(void *) */
static const LumaFunction* set_seed;        /* void MR::setFixedRandomSeed(u32) */
static const LumaFunction* get_seed;        /* u32 MR::getFixedRandomSeed() */

/* The longest plan line, argument list or fact the game half takes, in bytes with its end, and the most words of a
   line: the runner refuses a plan over them (luma_test.py LINE_BYTES, LINE_WORDS), and anything over them here is a
   failed line naming it, never a cut one. */
#define LINE_BYTES 1024u
#define LINE_WORDS 16u

static int32_t field_name = -1;      /* NameObj.mName */
static int32_t field_position = -1; /* LiveActor.mPosition */
static int32_t field_gravity = -1;  /* LiveActor.mGravity */
static int32_t field_is_mario = -1; /* GameDataHolder.mIsMario */
static int32_t field_user = -1;     /* GameDataHolder.mUser */
static int32_t field_handler = -1; /* SaveDataHandleSequence.mSaveDataHandler (optional: file copy) */
static int32_t field_mode = -1;     /* MarioActor.mPlayerMode */
static int32_t field_health = -1;   /* MarioActor.mHealth */
static int32_t class_live_actor = -1; /* LiveActor's class number (is_a) */
static int32_t class_mario_actor = -1; /* MarioActor's */

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
    char text[LINE_BYTES];
    uint32_t at;
    uint32_t cut; /* nonzero: more was put than fits; the fact is not written (fact) */
} Out;

static void out_empty(Out* out) {
    out->at = 0;
    out->cut = 0;
    out->text[0] = 0;
}

static void put(Out* out, const char* text) {
    if (out->cut != 0u) {
        return;
    }
    while (*text != 0 && out->at + 1 < sizeof(out->text)) {
        out->text[out->at++] = *text++;
    }
    if (*text != 0) {
        /* A cut text says so at its end: a fact that is cut is not written (fact), a message is. */
        static const char said[] = " (cut)";
        out->cut = 1u;
        out->at = (uint32_t)(sizeof(out->text) - sizeof(said));
        /* Never half a character: a UTF-8 character the cut falls in is dropped whole, so the
           runner's UTF-8 read of the message holds. text[at] is the first byte dropped; while it continues a
           character (10xxxxxx), that character began earlier. */
        while (out->at > 0u && ((unsigned char)out->text[out->at] & 0xC0u) == 0x80u) {
            out->at--;
        }
        for (uint32_t i = 0; i < sizeof(said); i++) {
            out->text[out->at + i] = said[i];
        }
        out->at += (uint32_t)(sizeof(said) - 1u);
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

/* A number as JSON, to 1/1000 (enough for positions in game units and for factors); an f64 is taken whole, never
   through float. */
static void put_float(Out* out, double value) {
    double v = value;
    /* NaN, the infinities and anything past 2^63 (whose whole part does not fit the conversion below) are null. */
    if (v != v || v >= 9223372036854775808.0 || v <= -9223372036854775808.0) {
        put(out, "null");
        return;
    }
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
    char buffer[LINE_BYTES];
    const char* word[LINE_WORDS];
    uint32_t count;
    uint32_t over; /* nonzero: the text was longer than LINE_BYTES - 1 or had more than LINE_WORDS words */
} Words;

static void split(Words* w, const char* line, char separator) {
    uint32_t n = 0;
    w->count = 0;
    w->over = 0;
    while (line[n] != 0 && n + 1 < sizeof(w->buffer)) {
        w->buffer[n] = line[n];
        n++;
    }
    if (line[n] != 0) {
        w->over = 1u;
    }
    w->buffer[n] = 0;
    char* at = w->buffer;
    while (*at != 0) {
        if (w->count == LINE_WORDS) {
            w->over = 1u;
            break;
        }
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

/* The first fact of the current line that did not fit, by key: the line then fails naming it (finish). */
static char cut_fact[128];

static void fact(const char* key, Out* json) {
    if (json->cut != 0u) {
        if (cut_fact[0] == 0) {
            uint32_t i = 0;
            for (; key[i] != 0 && i + 1 < sizeof(cut_fact); i++) {
                cut_fact[i] = key[i];
            }
            cut_fact[i] = 0;
        }
        return;
    }
    harness->fact(key, json->text);
}

/* Every line ends here: a line one of whose facts did not fit fails, naming the fact. */
static void finish(int ok, const char* why) {
    if (cut_fact[0] != 0) {
        static Out text;
        out_empty(&text);
        put(&text, "the fact ");
        put(&text, cut_fact);
        put(&text, " is longer than the harness's 1,023 bytes: not written");
        cut_fact[0] = 0;
        harness->done(0, text.text);
        return;
    }
    harness->done(ok, why);
}

static void fact_bool(const char* key, int value) {
    harness->fact(key, value ? "true" : "false");
}

static void fact_int(const char* key, int64_t value) {
    Out o;
    out_empty(&o);
    put_int(&o, value);
    fact(key, &o);
}

static void fact_text(const char* key, const char* value) {
    Out o;
    out_empty(&o);
    put_string(&o, value);
    fact(key, &o);
}

/* --- Objects by name ------------------------------------------------------------------------------------------------- */

/* Every NameObj made (NameObj::NameObj, so actors, areas, layouts, cameras...), in creation order. One that goes leaves
   a hole (null), so a destructor costs no move of the rest; holes are closed when the list is full or an arena goes. */
#define MOST_OBJECTS 65536u
#define MOST_OBJECTS_TEXT "65,536"
static void* objects[MOST_OBJECTS];
static uint32_t object_count;
static uint32_t objects_lost;

static void close_holes(void) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i] != 0) {
            objects[kept++] = objects[i];
        }
    }
    object_count = kept;
}

static int32_t on_object(LumaCall* call, void* user) {
    (void)user;
    if (object_count == MOST_OBJECTS) {
        close_holes();
    }
    if (object_count < MOST_OBJECTS) {
        objects[object_count++] = LUMA_THIS(call, void);
    } else {
        objects_lost++;
    }
    return 0;
}

/* A NameObj's destructor: it leaves the list, so its memory, used again, is never taken for it. The newest go first
   most often, so the search starts at the end. */
static int32_t on_object_gone(LumaCall* call, void* user) {
    (void)user;
    void* const gone = LUMA_THIS(call, void);
    for (uint32_t i = object_count; i-- > 0;) {
        if (objects[i] == gone) {
            objects[i] = 0;
            break;
        }
    }
    return 0;
}

static void* player_mario;

/* An arena about to go (a stage's scene, whatever changed the stage): what lies in it leaves the lists, the objects
   whose destructors never run with it and the player's Mario. Its child arenas come here each on its own. */
static int32_t on_arena_gone(LumaCall* call, void* user) {
    (void)user;
    void* const arena = LUMA_ARG(call, 1, void*);
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i] == 0) {
            continue;
        }
        void* in = 0;
        call1(arena_of, &objects[i], &in);
        if (in == arena) {
            objects[i] = 0;
        }
    }
    close_holes();
    if (player_mario != 0) {
        void* in = 0;
        call1(arena_of, &player_mario, &in);
        if (in == arena) {
            player_mario = 0;
        }
    }
    return 0;
}

/* Whether an object of the game is of a class or one made from it. */
static int object_is(const void* object, int32_t class_number) {
    return object != 0 && class_number >= 0 && core->is_a(object, class_number) != 0;
}

/* The n-th live object (creation order: the order the game made them, the list this file keeps) whose name is `name`,
   or with class_number >= 0 whose own class is that class or one made from it (harness revision 6, R6-5); null when
   there is none. Live: not destroyed, and for an actor not dead (MR::isDead). */
static void* object_found(const char* name, int32_t class_number, int64_t n) {
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i] == 0) {
            continue;
        }
        if (class_number >= 0) {
            if (!object_is(objects[i], class_number)) {
                continue;
            }
        } else {
            const char* const its = *(const char* const*)((const char*)objects[i] + field_name);
            if (its == 0 || !same(its, name)) {
                continue;
            }
        }
        unsigned char dead = 0;
        if (object_is(objects[i], class_live_actor)) {
            call1(is_dead, &objects[i], &dead);
        }
        if (dead == 0 && n-- == 0) {
            return objects[i];
        }
    }
    return 0;
}

/* The text after `prefix` when `text` starts with it; null otherwise. */
static const char* after_prefix(const char* text, const char* prefix) {
    for (; *prefix != 0; prefix++, text++) {
        if (*text != *prefix) {
            return 0;
        }
    }
    return text;
}

/* "<name>#<n>" or "class:<Class>#<n>" (the runner checked the class against the types file): its object, or null. */
static void* object_ref(const char* ref) {
    const char* const of_class = after_prefix(ref, "class:");
    static Words w;
    split(&w, of_class != 0 ? of_class : ref, '#');
    int good = 0;
    const int64_t n = w.count == 2u && w.over == 0u ? to_int(w.word[1], &good) : 0;
    if (w.count != 2u || !good) {
        return 0;
    }
    if (of_class == 0) {
        return object_found(w.word[0], -1, n);
    }
    const int32_t number = core->class_of(w.word[0]);
    return number >= 0 ? object_found(0, number, n) : 0;
}

/* "player", "object:<name>#<n>" or "class:<Class>#<n>". */
static void* object_of(const char* ref) {
    if (same(ref, "player")) {
        return player();
    }
    const char* const named = after_prefix(ref, "object:");
    return named != 0 ? object_ref(named) : after_prefix(ref, "class:") != 0 ? object_ref(ref) : 0;
}

/* Why a reference found nothing, as the test names it: "no live object <name>#<n>" or "no live object of class
   <Class> #<n>" (G-H6-2: a typo is told from a stage without the object), after `lead`. */
static const char* missing_text(const char* lead, const char* ref) {
    static Out why;
    out_empty(&why);
    put(&why, lead);
    if (objects_lost != 0) {
        put(&why, "more objects than the harness keeps (" MOST_OBJECTS_TEXT "), the object may be one of those; ");
    }
    put(&why, "no live object ");
    const char* const named = after_prefix(ref, "object:");
    const char* const of_class = after_prefix(ref, "class:");
    if (of_class != 0) {
        static char cls[128];
        uint32_t i = 0;
        for (; of_class[i] != 0 && of_class[i] != '#' && i + 1 < sizeof(cls); i++) {
            cls[i] = of_class[i];
        }
        cls[i] = 0;
        put(&why, "of class ");
        put(&why, cls);
        put(&why, " ");
        put(&why, of_class + i);
    } else {
        put(&why, named != 0 ? named : ref);
    }
    return why.text;
}

/* --- What a line asks ------------------------------------------------------------------------------------------------ */

typedef enum Pending { Pending_None, Pending_Stage, Pending_File, Pending_FileStep } Pending;
static Pending pending;
/* A file step (`file copy|load|erase|new`, harness 3.6): which, and the files; file_mario is the character a file is
   read as (save.character's; Mario until it is set). */
typedef enum FileOp { FileOp_Copy, FileOp_Load, FileOp_Erase, FileOp_New } FileOp;
static FileOp file_op;
static int32_t file_from, file_to;
static int file_mario = 1;
static char pending_stage[64];
static int32_t pending_scenario;
static int32_t pending_file; /* the user file `setup file` made current; 0 before one */
static uint32_t pending_frames;
static int pending_is_step;
static int32_t frame_now = -1;

static const char* const kForms[] = {"normal", "rainbow", "fire", "ice", "bee", "spring", "boo", "flying", 0, "tornado"};

/* player_mario (above): the player's Mario, as its own Mario::checkKeyLock last saw it this stage (0 until it runs on
   the new stage, and once its arena goes). */

static int32_t on_key_lock(LumaCall* call, void* user) {
    (void)user;
    player_mario = LUMA_THIS(call, void);
    return 0;
}

/* The game's own answer to "does the player take input": MarioModule::isInputDisable, which holds the first 75
   frames on a stage (Mario::checkKeyLock), the landing and stage-in animations and a remote demo (run 8). */
static int player_takes_input(void) {
    if (player_mario == 0) {
        return 0;
    }
    unsigned char disabled = 1;
    void* args[] = {player_mario};
    input_disabled->invoke(args, &disabled);
    return disabled == 0;
}

/* Ready, or the first condition still unmet and its value in `why` (harness revision 3, R3-1). */
static int stage_ready(Out* why) {
    const char* name = 0;
    int32_t scenario = 0;
    unsigned char opening = 1, starting = 1, demo = 1;
    call0(stage_name, &name);
    call0(scenario_no, &scenario);
    call0(opening_camera, &opening);
    call0(scenario_start, &starting);
    /* The stage-start demo (the scenario starter's "Welcome" layout, run 6) holds the player's input: ready once no
       demo runs. */
    call0(demo_active, &demo);
    out_empty(why);
    if (name == 0 || !same(name, pending_stage)) {
        put(why, "ready: stage ");
        put(why, pending_stage);
        put(why, " expected, found ");
        put(why, name != 0 ? name : "none");
    } else if (scenario != pending_scenario) {
        put(why, "ready: scenario ");
        put_int(why, pending_scenario);
        put(why, " expected, found ");
        put_int(why, scenario);
    } else if (opening != 0) {
        put(why, "ready: MR::isStageStateScenarioOpeningCamera still true");
    } else if (starting != 0) {
        put(why, "ready: MR::isExecScenarioStarter still true");
    } else if (demo != 0) {
        put(why, "ready: MR::isDemoActive still true");
    } else if (player() == 0) {
        put(why, "ready: no player");
    } else if (!player_takes_input()) {
        put(why, player_mario == 0 ? "ready: Mario::checkKeyLock has not run on this stage"
                                   : "ready: MarioModule::isInputDisable still true");
    } else {
        return 1;
    }
    return 0;
}

/* User file <n> made current as the file select does: loaded when the saves hold it, else made through the game's
   new-file path (which also writes it, so the next test continuing from this one finds it). pending_file: above. */
static void* the_current_file(void) {
    void* sequence = 0;
    call0(save_sequence, &sequence);
    if (sequence == 0) {
        return 0;
    }
    void* file = 0;
    void* args[] = {sequence};
    current_file->invoke(args, &file);
    return file;
}

static int file_is_created(void* file) {
    unsigned char created = 0;
    void* args[] = {file};
    file_created->invoke(args, &created);
    return created != 0;
}

static void setup_file(const char* text) {
    int good = 0;
    const int64_t n = to_int(text, &good);
    if (!good || n < 1 || n > 3) {
        finish(0, "file: a user file is 1, 2 or 3");
        return;
    }
    pending_file = (int32_t)n;
    void* const file = the_current_file();
    if (file == 0) {
        finish(0, "file: the game has no current user file yet");
        return;
    }
    int32_t index = pending_file;
    unsigned char mario = 1;
    void* load_args[] = {0, &index, &mario};
    load_file->invoke(load_args, 0);
    if (file_is_created(file)) {
        fact_text("setup.file", "loaded");
        finish(1, 0);
        return;
    }
    void* create_args[] = {0, &index};
    create_file->invoke(create_args, 0);
    pending_frames = 0;
    pending = Pending_File;
}

static void file_tick(void) {
    unsigned char busy = 1;
    call0(save_busy, &busy);
    if (busy != 0) {
        if (++pending_frames > 60u * 60u) {
            pending = Pending_None;
            finish(0, "file: the new file was not written within 3,600 frames");
        }
        return;
    }
    pending = Pending_None;
    void* const file = the_current_file();
    const int created = file != 0 && file_is_created(file);
    fact_text("setup.file", created ? "created" : "not created");
    finish(created, "file: the game's new-file path left the file not created");
}

/* `file copy <from> <to>`, `file load <n>`, `file erase <n>`, `file new <n>`: the file select's operations on the saves of
   this run, through the game's own GameSequenceFunction; answered when the save sequence has ended, the result read
   back (file_step_tick). Copy first puts the current file into its slot when it is the source, as the first step of
   the game's own save does (SaveDataHandleSequence::exeSave), so the copy holds what the game and the mods set on it. */
static void do_file(const Words* w) {
    const char* const verb = w->word[1];
    const int copy = same(verb, "copy");
    if (!copy && !same(verb, "load") && !same(verb, "erase") && !same(verb, "new")) {
        finish(0, "file: copy, load, erase or new");
        return;
    }
    if (w->count != (copy ? 4u : 3u)) {
        finish(0, copy ? "file copy takes two user files" : "file load, erase and new take one user file");
        return;
    }
    int g1 = 0, g2 = 1;
    const int64_t a = to_int(w->word[2], &g1);
    const int64_t b = copy ? to_int(w->word[3], &g2) : a;
    if (!g1 || !g2 || a < 1 || a > 3 || b < 1 || b > 3) {
        finish(0, "file: a user file is 1, 2 or 3");
        return;
    }
    if (copy && a == b) {
        finish(0, "file copy: from and to are the same file");
        return;
    }
    if (the_current_file() == 0) {
        finish(0, "file: the game has no current user file yet");
        return;
    }
    int32_t from = (int32_t)a, to = (int32_t)b;
    file_from = from;
    file_to = to;
    if (copy) {
        file_op = FileOp_Copy;
        void* holder = 0;
        call0(game_data, &holder);
        if (holder != 0 && field_user >= 0 && *(const int32_t*)((const char*)holder + field_user) == from) {
            void* sequence = 0;
            call0(save_sequence, &sequence);
            void* handler = sequence != 0 && field_handler >= 0 ? *(void* const*)((const char*)sequence + field_handler) : 0;
            if (handler == 0 || store_file == 0) {
                finish(0, "file copy: the game has no SaveDataHandler::storeUserFile by name to put the current file into its slot");
                return;
            }
            void* current = the_current_file();
            void* store_args[] = {handler, &current};
            store_file->invoke(store_args, 0);
        }
        void* args[] = {0, &to, &from};
        copy_file->invoke(args, 0);
    } else if (same(verb, "load")) {
        file_op = FileOp_Load;
        unsigned char mario = (unsigned char)file_mario;
        void* args[] = {0, &from, &mario};
        load_file->invoke(args, 0);
    } else if (same(verb, "erase")) {
        file_op = FileOp_Erase;
        void* args[] = {0, &from};
        delete_file->invoke(args, 0);
    } else {
        file_op = FileOp_New;
        void* args[] = {0, &from};
        create_file->invoke(args, 0);
    }
    pending_frames = 0;
    pending = Pending_FileStep;
}

/* The file step's end: the save sequence has stopped, and its answer and the current file say what the step did. The
   fact: file.copied {"from","to"}, file.loaded n, file.erased n, file.created n. */
static void file_step_tick(void) {
    unsigned char busy = 1;
    call0(save_busy, &busy);
    if (busy != 0) {
        if (++pending_frames > 60u * 60u) {
            pending = Pending_None;
            finish(0, "file: the save sequence did not end within 3,600 frames");
        }
        return;
    }
    pending = Pending_None;
    unsigned char success = 0;
    call0(save_success, &success);
    void* const file = the_current_file();
    void* holder = 0;
    call0(game_data, &holder);
    const int32_t held = holder != 0 && field_user >= 0 ? *(const int32_t*)((const char*)holder + field_user) : file_from;
    const int created = file != 0 && file_is_created(file);
    Out o;
    out_empty(&o);
    switch (file_op) {
    case FileOp_Copy:
        if (!success) {
            finish(0, "file copy: the game's save sequence did not end done");
            return;
        }
        put(&o, "{\"from\":");
        put_int(&o, file_from);
        put(&o, ",\"to\":");
        put_int(&o, file_to);
        put(&o, "}");
        fact("file.copied", &o);
        break;
    case FileOp_Load:
        if (held != file_from || !success) {
            finish(0, "file load: the game did not make that file the current one");
            return;
        }
        fact_int("file.loaded", file_from);
        break;
    case FileOp_Erase:
        /* The current file is the one read back: an erase of another file is told by the sequence's answer. */
        if (!success || (held == file_from && created)) {
            finish(0, "file erase: the file is still there, or the game's save sequence did not end done");
            return;
        }
        fact_int("file.erased", file_from);
        break;
    default:
        if (!success || !created || held != file_from) {
            finish(0, "file new: the game's new-file path left the file not created, or not current");
            return;
        }
        fact_int("file.created", file_from);
        break;
    }
    finish(1, 0);
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
        finish(0, "the game has no stage of that name");
        return;
    }
    for (uint32_t i = 0; i <= length(name); i++) {
        pending_stage[i] = name[i];
    }
    pending_scenario = (int32_t)scenario;
    pending_frames = 0;
    pending_is_step = is_step;
    pending = Pending_Stage;
    player_mario = 0;
    object_count = 0;
    objects_lost = 0;
    const char* stage = pending_stage;
    void* args[] = {0, (void*)&stage, &pending_scenario};
    change_stage->invoke(args, 0);
}

static void finish_stage(void) {
    Out o;
    out_empty(&o);
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
    finish(1, 0);
}

static void setup_save(const Words* w) {
    const char* const item = w->word[1];
    int good = 0;
    if (same(item, "save.character") && w->count == 3u) {
        void* holder = 0;
        call0(game_data, &holder);
        if (holder == 0 || field_is_mario < 0 || field_user < 0) {
            finish(0, field_is_mario < 0 ? "not built yet: GameDataHolder has no fields by name (task 1.2)" : "save.character: no game data to set");
            return;
        }
        *(unsigned char*)((char*)holder + field_is_mario) = same(w->word[2], "luigi") ? 0u : 1u;
        file_mario = !same(w->word[2], "luigi");
        /* The holder's user file: the one `setup file` made current; else as the load set it. */
        if (pending_file != 0) {
            *(int32_t*)((char*)holder + field_user) = pending_file;
        }
        const int mario = *(const unsigned char*)((const char*)holder + field_is_mario) != 0;
        fact_text("setup.save.character", mario ? "mario" : "luigi");
        finish(same(w->word[2], mario ? "mario" : "luigi"), "save.character did not take");
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
        finish(passed == want, "save.spin did not take");
        return;
    }
    if (same(item, "save.flag") && w->count == 3u) {
        if (!ask_text(flag_exists, w->word[2])) {
            finish(0, "save.flags: the game has no event flag of that name");
            return;
        }
        call1(flag_on, (void*)&w->word[2], 0);
        const int on = ask_text(is_flag_on, w->word[2]);
        Out key;
    out_empty(&key);
        put(&key, "setup.save.flags.");
        put(&key, w->word[2]);
        fact_bool(key.text, on);
        finish(on, "save.flags did not take");
        return;
    }
    if (same(item, "save.value") && w->count == 4u) {
        const int64_t value = to_int(w->word[3], &good);
        if (!good || value < 0 || value > 65535) {
            finish(0, "save.values: a value is 0 to 65535");
            return;
        }
        uint16_t v16 = (uint16_t)value;
        void* args[] = {0, (void*)&w->word[2], &v16};
        set_value->invoke(args, 0);
        uint32_t back = 0;
        call1(get_value, (void*)&w->word[2], &back);
        Out key;
    out_empty(&key);
        put(&key, "setup.save.values.");
        put(&key, w->word[2]);
        fact_int(key.text, back);
        finish(back == (uint32_t)value, "save.values did not take");
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
            finish(0, "not built yet: setting a number of stars other than the file's own");
            return;
        }
        finish(good, "a number of stars is a whole number");
        return;
    }
    finish(0, "not a save item the harness knows");
}

static void setup_player(const Words* w) {
    const char* const item = w->word[1];
    void* const actor = player();
    int good = 1;
    if (actor == 0) {
        finish(0, "player: there is no player (no stage)");
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
            static Out ref;
            out_empty(&ref);
            put(&ref, w->word[2]);
            put(&ref, "#");
            put(&ref, w->word[3]);
            void* const object = object_ref(ref.text);
            if (object == 0) {
                /* Named as the test names it (name#n, class:Class#n). */
                finish(0, missing_text("player.at: ", ref.text));
                return;
            }
            if (!object_is(object, class_live_actor)) {
                finish(0, "player.at: that object is no actor (it has no position)");
                return;
            }
            const Vec3* const at = (const Vec3*)((const char*)object + field_position);
            /* Above the object by its own gravity (it may stand on another planet than the player); the player's
               when the object has none (no gravity of its own, zero). */
            const Vec3* own = field_gravity >= 0 ? (const Vec3*)((const char*)object + field_gravity) : 0;
            const Vec3* down = 0;
            if (own != 0 && own->x * own->x + own->y * own->y + own->z * own->z > 0.25f) {
                down = own;
            } else {
                call0(player_gravity, &down);
            }
            const Vec3 g = down != 0 ? *down : (Vec3){0.0f, -1.0f, 0.0f};
            place.x = at->x - g.x * 150.0f;
            place.y = at->y - g.y * 150.0f;
            place.z = at->z - g.z * 150.0f;
        }
        if (!good) {
            finish(0, "player.position is three numbers");
            return;
        }
        call1(set_player_pos, &place, 0); /* const Vec3f &: the argument is the vector's address (luma.h) */
        Vec3* back = 0;
        call0(player_pos, &back);
        Out o;
    out_empty(&o);
        if (back != 0) {
            put_vec(&o, back);
        } else {
            put(&o, "null");
        }
        fact(same(item, "player.position") ? "setup.player.position" : "setup.player.at", &o);
        const float dx = back != 0 ? back->x - place.x : 1e9f, dy = back != 0 ? back->y - place.y : 0.0f, dz = back != 0 ? back->z - place.z : 0.0f;
        finish(dx * dx + dy * dy + dz * dz < 1.0f, "the player is not where he was put");
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
            finish(0, "player.form: the game has no form of that name");
            return;
        }
        uint16_t m = (uint16_t)mode;
        unsigned char morph = 1;
        void* args[] = {actor, &m, &morph};
        set_mode->invoke(args, 0);
        const uint16_t back = *(const uint16_t*)((const char*)actor + field_mode);
        fact_text("setup.player.form", back < sizeof(kForms) / sizeof(kForms[0]) && kForms[back] != 0 ? kForms[back] : "?");
        finish(back == m, "player.form did not take");
        return;
    }
    if (same(item, "player.life") && w->count == 3u) {
        const int64_t life = to_int(w->word[2], &good);
        if (!good || life < 1 || field_health < 0) {
            finish(0, "player.life is 1 or more");
            return;
        }
        *(uint32_t*)((char*)actor + field_health) = (uint32_t)life;
        const uint32_t back = *(const uint32_t*)((const char*)actor + field_health);
        fact_int("setup.player.life", back);
        finish(back == (uint32_t)life, "player.life did not take");
        return;
    }
    finish(0, "not a player item the harness knows");
}

/* `call <fact or -> <name>\t<signature>\t<argument>\t...`: arguments as the plan writes them (numbers, true, false,
   strings, "x y z" vectors, player, object:<name>#<n>), each converted to the parameter's type from the signature. */
#define MOST_ARGS 8
static void do_call(const char* key, const char* rest) {
    static Words parts;
    split(&parts, rest, '\t');
    if (parts.over != 0u) {
        finish(0, "call: more than 14 arguments, or longer than 1,023 bytes: not taken");
        return;
    }
    if (parts.count < 2u) {
        finish(0, "a call needs a name and a signature");
        return;
    }
    const LumaFunction* const f = core->function(parts.word[0], parts.word[1]);
    if (f == 0) {
        finish(0, "the game has no function of that name and signature");
        return;
    }
    /* The parameter types, between the signature's outer parentheses. */
    const char* sig = parts.word[1];
    const char* open = sig;
    while (*open != 0 && *open != '(') {
        open++;
    }
    static char params[LINE_BYTES];
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
    if (types.over != 0u) {
        finish(0, "call: a signature of more than 16 parameters: not taken");
        return;
    }
    if (n == 0u) {
        types.count = 0;
    }
    if (types.count != parts.count - 2u || types.count > MOST_ARGS) {
        finish(0, "the call's arguments do not match its signature");
        return;
    }
    union Value {
        float f;
        double d;
        int32_t i;
        uint32_t u;
        uint16_t h;
        int16_t sh;
        unsigned char b;
        signed char sb;
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
        } else if (same(type, "s16")) {
            values[i].sh = (int16_t)to_int(text, &good);
            args[i + 1] = &values[i].sh;
        } else if (same(type, "u8")) {
            values[i].b = (unsigned char)to_int(text, &good);
            args[i + 1] = &values[i].b;
        } else if (same(type, "s8")) {
            values[i].sb = (signed char)to_int(text, &good);
            args[i + 1] = &values[i].sb;
        } else if (same(type, "f64") || same(type, "double")) {
            values[i].d = to_float(text, &good);
            args[i + 1] = &values[i].d;
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
            int g1 = 0, g2 = 0, g3 = 0;
            if (after_prefix(text, "object:") != 0 || after_prefix(text, "class:") != 0) {
                /* A live actor's place (harness revision 8): its position as the game holds it. */
                void* const object = object_of(text);
                if (object == 0) {
                    finish(-1, missing_text("call: ", text));
                    return;
                }
                if (!object_is(object, class_live_actor)) {
                    finish(0, "call: an argument is no actor (it has no position), and the parameter takes a place");
                    return;
                }
                values[i].v = *(const Vec3*)((const char*)object + field_position);
                g1 = g2 = g3 = 1;
                good = 1;
            } else {
                split(&xyz, text, ' ');
                good = xyz.count == 3u && xyz.over == 0u;
                values[i].v.x = good ? to_float(xyz.word[0], &g1) : 0.0f;
                values[i].v.y = good ? to_float(xyz.word[1], &g2) : 0.0f;
                values[i].v.z = good ? to_float(xyz.word[2], &g3) : 0.0f;
                good = good && g1 && g2 && g3;
            }
            vec_pointers[i] = &values[i].v;
            /* A pointer parameter: args[n] points at the pointer. A reference or a value: at the vector itself (luma.h). */
            const int pointer = same(type, "Vec3f *") || same(type, "const Vec3f *");
            args[i + 1] = pointer ? (void*)&vec_pointers[i] : (void*)&values[i].v;
        } else if (same(type, "LiveActor *") || same(type, "const LiveActor *") || same(type, "NameObj *") || same(type, "const NameObj *") ||
                   same(type, "MarioActor *")) {
            values[i].p = object_of(text);
            args[i + 1] = &values[i].p;
            if (values[i].p == 0) {
                finish(-1, same(text, "player") ? "call: there is no player" : missing_text("call: ", text));
                return;
            }
            const int32_t needs = same(type, "MarioActor *") ? class_mario_actor
                                 : same(type, "LiveActor *") || same(type, "const LiveActor *") ? class_live_actor
                                                                                                : -1;
            if (needs >= 0 && !object_is(values[i].p, needs)) {
                finish(0, needs == class_mario_actor ? "call: a MarioActor * takes the player" : "call: an argument is no actor, and the parameter takes one");
                return;
            }
        } else {
            finish(0, "a parameter type the harness cannot pass");
            return;
        }
        if (!good) {
            finish(0, "an argument does not fit its parameter, or names no live object");
            return;
        }
    }
    /* The result, by the signature's return type (the text before the parentheses). */
    static char ret[LINE_BYTES];
    uint32_t r = 0;
    while (sig[r] != 0 && sig[r] != '(' && r + 1 < sizeof(ret)) {
        ret[r] = sig[r];
        r++;
    }
    while (r > 0 && ret[r - 1] == ' ') {
        r--;
    }
    ret[r] = 0;
    /* Only a result the harness knows is taken: any other (an enum, s64, a struct by value) could be wider than
       `result` or read as the wrong bytes, so the call is not made. A pointer is "is there one". */
    static const char* const kResults[] = {"void", "f32", "float", "f64", "double", "s32", "int", "u32", "u16", "s16", "u8", "s8", "bool",
                                           "const char *", "Vec3f *", "const Vec3f *", "TVec3f *", "const TVec3f *", "Vec3f", "TVec3f", 0};
    int known = r > 0 && ret[r - 1] == '*';
    for (uint32_t k = 0; kResults[k] != 0 && !known; k++) {
        known = same(ret, kResults[k]);
    }
    if (!known) {
        static Out text;
        out_empty(&text);
        put(&text, "call: a result type the harness cannot take: ");
        put(&text, ret);
        finish(0, text.text);
        return;
    }
    /* Filled with 0xA5, not zero (run 16, R16-1): a function that runs writes its result over it, so a result still
       0 after a skip is the kernel's value-initialisation (Z-2), never the harness's. */
    union Value result;
    for (uint32_t b = 0; b < sizeof(result); b++) {
        ((unsigned char*)&result)[b] = 0xA5;
    }
    f->invoke(args, same(ret, "void") ? (void*)0 : (void*)&result);
    if (same(key, "-")) {
        finish(1, 0);
        return;
    }
    Out o;
    out_empty(&o);
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
    } else if (same(ret, "s16")) {
        put_int(&o, result.sh);
    } else if (same(ret, "u8")) {
        put_int(&o, result.b);
    } else if (same(ret, "s8")) {
        put_int(&o, result.sb);
    } else if (same(ret, "f64") || same(ret, "double")) {
        put_float(&o, result.d);
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
    finish(1, 0);
}

/* `read <fact> <of> <Class.field> <type>`. */
static void do_read(const Words* w) {
    if (w->count < 5u) {
        finish(0, "a read needs a fact, an object, a field and a type");
        return;
    }
    static Words cf;
    split(&cf, w->word[3], '.');
    /* The type may hold spaces: the rest of the line. */
    static char type[LINE_BYTES];
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
    if (cf.over != 0u) {
        finish(0, "read: a field name longer than 1,023 bytes");
        return;
    }
    void* const object = object_of(w->word[2]);
    const int32_t offset = cf.count == 2u ? core->field(cf.word[0], cf.word[1], type) : -1;
    if (object == 0) {
        // Revision 7 (G-H6-2): the object a step names is not there: the test is not set up, not failed.
        finish(-1, same(w->word[2], "player") ? "read: there is no player" : missing_text("read: ", w->word[2]));
        return;
    }
    if (offset < 0) {
        finish(0, "the game has no field of that name and type");
        return;
    }
    if (!object_is(object, core->class_of(cf.word[0]))) {
        finish(0, "read: the object is not of the field's class");
        return;
    }
    const char* const at = (const char*)object + offset;
    Out o;
    out_empty(&o);
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
        finish(0, "a field type the harness cannot read");
        return;
    }
    fact(w->word[1], &o);
    finish(1, 0);
}

static void do_line(const char* line) {
    static Words w;
    split(&w, line, ' ');
    /* A call's words are its tab-separated arguments, read from the line itself (do_call checks them). */
    if (w.over != 0u && !(w.count >= 2u && same(w.word[0], "call") && length(line) < LINE_BYTES)) {
        finish(0, "a plan line longer than 1,023 bytes or of more than 16 words: not taken");
        return;
    }
    if (w.count == 0u) {
        finish(0, "an empty line");
        return;
    }
    if (same(w.word[0], "frame") && w.count == 2u) {
        int good = 0;
        frame_now = (int32_t)to_int(w.word[1], &good);
        finish(good, "a frame number");
        return;
    }
    if (same(w.word[0], "setup") && w.count >= 2u) {
        const char* const item = w.word[1];
        if (same(item, "file") && w.count == 3u) {
            setup_file(w.word[2]);
        } else if (same(item, "stage") && w.count == 4u) {
            start_stage(w.word[2], w.word[3], 0);
        } else if (item[0] == 's' && item[1] == 'a' && item[2] == 'v' && item[3] == 'e' && item[4] == '.') {
            setup_save(&w);
        } else if (item[0] == 'p' && item[1] == 'l' && item[2] == 'a' && item[3] == 'y' && item[4] == 'e' && item[5] == 'r' && item[6] == '.') {
            setup_player(&w);
        } else if (same(item, "seed") && w.count == 3u) {
            /* 0: the game's own, from the stage's name as each stage starts. Another number seeds the generator now
               and at every stage start after (the setup stage's comes next), read back as the game holds it. */
            int good = 0;
            const int64_t n = to_int(w.word[2], &good);
            if (!good || n < 0 || n > 4294967295ll) {
                finish(0, "seed: a whole number from 0 to 4294967295");
                return;
            }
            uint32_t seed = (uint32_t)n;
            if (set_seed == 0 || get_seed == 0) {
                fact_int("setup.seed", 0);
                finish(seed == 0u, "seed: the game has no MR::setFixedRandomSeed and MR::getFixedRandomSeed");
                return;
            }
            call1(set_seed, &seed, 0);
            uint32_t held = 0;
            call0(get_seed, &held);
            fact_int("setup.seed", (int64_t)held);
            finish(held == seed, "seed did not take");
        } else if (same(item, "language") && w.count == 3u) {
            /* The runner set the language as the setting is set; the game's own name for the language it took. */
            const char* name = 0;
            call0(language_name, &name);
            fact_text("setup.language", name != 0 ? name : "");
            finish(name != 0 && same(name, w.word[2]), "language did not take: the game took the one setup.language holds (a disc offers only its own languages)");
        } else if (same(item, "done")) {
            fact_bool("setup.done", 1);
            finish(1, 0);
        } else {
            finish(0, "not a setup item the harness knows");
        }
        return;
    }
    if (same(w.word[0], "stage") && w.count == 3u) {
        start_stage(w.word[1], w.word[2], 1);
        return;
    }
    if (same(w.word[0], "file") && w.count >= 3u) {
        do_file(&w);
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
    finish(0, "not a line the game half knows");
}

static int32_t on_frame(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (pending == Pending_File) {
        file_tick();
        if (pending == Pending_File) {
            return 0;
        }
    }
    if (pending == Pending_FileStep) {
        file_step_tick();
        if (pending == Pending_FileStep) {
            return 0;
        }
    }
    if (pending == Pending_Stage) {
        static Out why;
        if (stage_ready(&why)) {
            finish_stage();
        } else if (++pending_frames > 60u * 120u) {
            pending = Pending_None;
            put(&why, " (after 7,200 frames)");
            finish(0, why.text);
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
    harness = (const LumaHarness*)core->service(mod, LUMA_HARNESS_NAME, LUMA_HARNESS_MAJOR, 0);
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
    LUMA_FUNCTION(demo_active, "MR::isDemoActive", "bool()"),
    LUMA_FUNCTION(save_sequence, "Game/System/GameSequenceFunction.cpp#getSaveDataHandleSequence", "SaveDataHandleSequence *()"),
    LUMA_FUNCTION(current_file, "SaveDataHandleSequence::getCurrentUserFile", "UserFile *()"),
    LUMA_FUNCTION(file_created, "UserFile::isCreated", "bool() const"),
    LUMA_FUNCTION(load_file, "GameSequenceFunction::startGameDataLoadSequence", "void(int, bool)"),
    LUMA_FUNCTION(create_file, "GameSequenceFunction::startCreateUserFileSequence", "void(int)"),
    LUMA_FUNCTION(delete_file, "GameSequenceFunction::startDeleteUserFileSequence", "void(int)"),
    LUMA_FUNCTION(copy_file, "GameSequenceFunction::startCopyUserFileSequence", "void(int, int)"),
    LUMA_FUNCTION(save_success, "GameSequenceFunction::isSuccessSaveDataHandleSequence", "bool()"),
    {LUMA_IMPORT_FUNCTION, LUMA_OPTIONAL, "SaveDataHandler::storeUserFile", "void(const UserFile *)", &store_file, 0, 0, 0},
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "SaveDataHandleSequence.mSaveDataHandler", "SaveDataHandler *", &field_handler, 0, 0, 0},
    LUMA_FUNCTION(save_busy, "GameSequenceFunction::isActiveSaveDataHandleSequence", "bool()"),
    LUMA_FIELD(field_name, "NameObj.mName", "const char *"),
    LUMA_FIELD(field_position, "LiveActor.mPosition", "Vec3f"),
    LUMA_FIELD(field_gravity, "LiveActor.mGravity", "Vec3f"),
    /* Optional: a class outside the game's object tree may have no fields by name yet (census D3, task 1.2); the item
       that needs one then fails, naming it. */
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "GameDataHolder.mIsMario", "bool", &field_is_mario, 0, 0, 0},
    {LUMA_IMPORT_FIELD, LUMA_OPTIONAL, "GameDataHolder.mUser", "s32", &field_user, 0, 0, 0},
    LUMA_FIELD(field_mode, "MarioActor.mPlayerMode", "u16"),
    LUMA_FIELD(field_health, "MarioActor.mHealth", "u32"),
    LUMA_HOOK_ON("NameObj::NameObj", "(const char *)", LUMA_AFTER, on_object),
    LUMA_CLASS(class_live_actor, "LiveActor"),
    LUMA_CLASS(class_mario_actor, "MarioActor"),
    LUMA_HOOK_ON("NameObj::~NameObj", "()", LUMA_AFTER, on_object_gone),
    LUMA_FUNCTION(arena_of, "NebulaArena::findFromRoot", "NebulaArena *(void *)"),
    /* Optional: only `seed` other than 0 needs them; without them that item fails, naming them. */
    {LUMA_IMPORT_FUNCTION, LUMA_OPTIONAL, "MR::setFixedRandomSeed", "void(u32)", &set_seed, 0, 0, 0},
    {LUMA_IMPORT_FUNCTION, LUMA_OPTIONAL, "MR::getFixedRandomSeed", "u32()", &get_seed, 0, 0, 0},
    LUMA_HOOK_ON("NebulaArena::destroy", "void(NebulaArena *)", LUMA_BEFORE, on_arena_gone),
    LUMA_FUNCTION(input_disabled, "MarioModule::isInputDisable", "bool() const"),
    LUMA_HOOK_ON("Mario::checkKeyLock", "void()", LUMA_AFTER, on_key_lock),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, on_frame),
};

LUMA_MOD(imports, start, 0);
