/* Harness test mod "saveprobe", for test 12 (continue_from).
   Option-free: what it does depends on the game event value "MessageAlreadyRead" it finds at the first player frame
   on its test's stage, EggStarGalaxy (the title has a player too, before the harness's setup: run 6, INBOX-G 17), in
   which the player takes input (MarioModule::isInputDisable false on the Mario of Mario::checkKeyLock, as the
   harness's ready, revision 3): the save starts as a player could start it, after the stage's first 75 frames.
   - Value 0 (a new file): sets it to 4660 and starts the player's own save,
     GameSequenceFunction::startGameDataSaveSequence(false, false): the confirm window "System_Save00", Yes on its
     "Right" pane (YesNoController), storing the current file (SaveDataHandler::storeUserFile in
     SaveDataHandleSequence::exeSave) on Yes, then the done window "System_Save02", closed by A.
     startSaveAllUserFileSequence, used before, writes the handler's stored copies only, without the current file's
     changes (run 7, INBOX-G 22).
     The test presses the buttons itself, as a player with a pad: no button is pointed until a direction moves the
     menu focus (NebulaMenuFocus; A alone chose nothing in run 9), so RIGHT, which focuses Yes, then A, then A on the
     done window. A window takes a press only while it waits (SysInfoWindow::exeWait, a press edge), so the probe
     writes the fact "ready" when one of the two windows has waited 30 frames, past its buttons' appearing ("confirm"
     for System_Save00, "done" for System_Save02), and "focused" true once the menu focus is on (after RIGHT).
     It writes "saved" true once GameSequenceFunction::isActiveSaveDataHandleSequence goes false again, with
     "save_ok" from isSuccessSaveDataHandleSequence.
   - Any other value: writes it as the fact "found".
   The first test expects saveprobe.saved true and save_ok true; the second, continuing, expects saveprobe.found 4660. */
#include <luma.h>

static const LumaCore* game;
static const LumaMod* self;
static const LumaFunction* get_value;   /* u32 GameDataFunction::getGameEventValue(const char *) */
static const LumaFunction* set_value;   /* void GameDataFunction::setGameEventValue(const char *, u16) */
static const LumaFunction* save_start;  /* void GameSequenceFunction::startGameDataSaveSequence(bool, bool) */
static const LumaFunction* save_active; /* bool GameSequenceFunction::isActiveSaveDataHandleSequence() */
static const LumaFunction* save_ok;     /* bool GameSequenceFunction::isSuccessSaveDataHandleSequence() */
static const LumaFunction* stage_name;  /* const char *MR::getCurrentStageName() */
static const LumaFunction* input_off;   /* bool MarioModule::isInputDisable() const */
static void* player_mario;              /* the Mario that Mario::checkKeyLock last ran on */
static const LumaFunction* focus_on;    /* bool NebulaMenuFocus::isActive() */
static int32_t focus_said;

static int32_t phase;         /* 0 first frame, 1 saving, 2 done */
static const char* window_id; /* the message of the window that appeared last, while saving; null otherwise */
static int32_t waited;        /* frames that window has waited */
static const char* const cName = "MessageAlreadyRead";
static const char* const cStage = "EggStarGalaxy";
static const char* const cConfirm = "System_Save00";
static const char* const cDone = "System_Save02";
static const int32_t cReady = 30;

static int32_t same(const char* a, const char* b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    while (*b != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int32_t on_test_stage(void) {
    const char* name = 0;
    void* args[] = {0};
    stage_name->invoke(args, &name);
    return same(name, cStage);
}

/* After Mario::checkKeyLock: the player's Mario (Mario is a MarioModule first). */
static int32_t after_key_lock(LumaCall* call, void* user) {
    (void)user;
    player_mario = LUMA_THIS(call, void);
    return 0;
}

static int32_t player_takes_input(void) {
    if (player_mario == 0) {
        return 0;
    }
    uint8_t off = 1;
    void* args[] = {player_mario};
    input_off->invoke(args, &off);
    return off == 0;
}

static void fact_number(const char* head, uint32_t value) {
    char line[64];
    char digits[12];
    int32_t count = 0;
    char* at = line;
    while (*head != 0) {
        *at++ = *head++;
    }
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count > 0) {
        *at++ = digits[--count];
    }
    *at = 0;
    game->log(self, line);
}

static uint8_t call_bool(const LumaFunction* fn) {
    uint8_t result = 0;
    void* args[] = {0};
    fn->invoke(args, &result);
    return result;
}

/* Before SysInfoWindow::appear(id, type, pos, message type): which window this is, while our save runs. */
static int32_t before_appear(LumaCall* call, void* user) {
    (void)user;
    const char* id = LUMA_ARG(call, 1, const char*);
    window_id = (phase == 1 && (same(id, cConfirm) || same(id, cDone))) ? id : 0;
    waited = 0;
    return 0;
}

/* After SysInfoWindow::exeWait: once a frame while a window waits for its answer. */
static int32_t after_wait(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (window_id == 0) {
        return 0;
    }
    if (++waited == cReady) {
        game->log(self, same(window_id, cConfirm) ? "fact ready \"confirm\"" : "fact ready \"done\"");
    } else if (waited > cReady && !focus_said && same(window_id, cConfirm) && call_bool(focus_on)) {
        game->log(self, "fact focused true");
        focus_said = 1;
    }
    return 0;
}

static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (phase == 0) {
        if (!on_test_stage() || !player_takes_input()) {
            return 0;
        }
        uint32_t value = 0;
        const char* name = cName;
        void* get_args[] = {0, &name};
        get_value->invoke(get_args, &value);
        if (value != 0u) {
            fact_number("fact found ", value);
            phase = 2;
            return 0;
        }
        uint16_t wanted = 4660u;
        void* set_args[] = {0, &name, &wanted};
        set_value->invoke(set_args, 0);
        phase = 1;
        uint8_t remind = 0;
        uint8_t quit_message = 0;
        void* save_args[] = {0, &remind, &quit_message};
        save_start->invoke(save_args, 0);
        game->log(self, "fact asked true");
    } else if (phase == 1 && !call_bool(save_active)) {
        game->log(self, "fact saved true");
        game->log(self, call_bool(save_ok) ? "fact save_ok true" : "fact save_ok false");
        phase = 2;
        window_id = 0;
    }
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    game = core;
    self = mod;
    phase = 0;
    window_id = 0;
    waited = 0;
    player_mario = 0;
    focus_said = 0;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(get_value, "GameDataFunction::getGameEventValue", "u32(const char *)"),
    LUMA_FUNCTION(set_value, "GameDataFunction::setGameEventValue", "void(const char *, u16)"),
    LUMA_FUNCTION(save_start, "GameSequenceFunction::startGameDataSaveSequence", "void(bool, bool)"),
    LUMA_FUNCTION(save_active, "GameSequenceFunction::isActiveSaveDataHandleSequence", "bool()"),
    LUMA_FUNCTION(save_ok, "GameSequenceFunction::isSuccessSaveDataHandleSequence", "bool()"),
    LUMA_FUNCTION(stage_name, "MR::getCurrentStageName", "const char *()"),
    LUMA_FUNCTION(input_off, "MarioModule::isInputDisable", "bool() const"),
    LUMA_FUNCTION(focus_on, "NebulaMenuFocus::isActive", "bool()"),
    LUMA_HOOK_ON("Mario::checkKeyLock", "void()", LUMA_AFTER, after_key_lock),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
    LUMA_HOOK_ON("SysInfoWindow::appear", "void(const char *, SysInfoWindow::SysInfoType, SysInfoWindow::SysInfoTextPos, "
                 "SysInfoWindow::SysInfoMessageType)",
                 LUMA_BEFORE, before_appear),
    LUMA_HOOK_ON("SysInfoWindow::exeWait", "void()", LUMA_AFTER, after_wait),
};

LUMA_MOD(imports, start, 0);
