/* Position and speed display (Gecko census, MOD-PLAN M9): The player's place and speed in the screen's top-left corner. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

/* Built with no C library: numbers are written by hand. */
static char* put_text(char* out, const char* text) {
    while (*text != 0) {
        *out++ = *text++;
    }
    return out;
}

static char* put_number(char* out, float value) {
    char digits[12];
    int count = 0;
    int whole = (int)(value < 0.0f ? -value : value);
    if (value < 0.0f) {
        *out++ = '-';
    }
    do {
        digits[count++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole != 0 && count < 11);
    while (count != 0) {
        *out++ = digits[--count];
    }
    return out;
}

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    NebulaActor* player = game->player();
    float at[3], velocity[3];
    char line[96];
    char* out;
    (void)user;
    if (player == 0) {
        return data->value;
    }
    game->get_position(player, at);
    game->get_velocity(player, velocity);
    out = put_text(line, "X ");
    out = put_number(out, at[0]);
    out = put_text(out, "  Y ");
    out = put_number(out, at[1]);
    out = put_text(out, "  Z ");
    out = put_number(out, at[2]);
    *out = 0;
    game->draw_text(0, 0.02f, 0.02f, 0.03f, 0xFFFFFFFFu, line);
    out = put_text(line, "Speed ");
    out = put_number(out, velocity[0]);
    out = put_text(out, " ");
    out = put_number(out, velocity[1]);
    out = put_text(out, " ");
    out = put_number(out, velocity[2]);
    *out = 0;
    game->draw_text(1, 0.02f, 0.06f, 0.03f, 0xFFFFFFFFu, line);
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
