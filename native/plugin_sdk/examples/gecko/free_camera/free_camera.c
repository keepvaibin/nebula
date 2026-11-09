/* Free camera (Gecko census, MOD-PLAN M9): Map button: the camera stays where it is and the d-pad moves it; map again gives it back. */
#include <nebula_mod_api.h>

static const NebulaModApi* game;
static const NebulaPlugin* me;

static int free_on;
static float place[3];
static float look[3];

static int32_t stage_frame(const NebulaModEventData* data, void* user) {
    const uint32_t pressed = game->buttons_pressed();
    const uint32_t held = game->buttons_held();
    (void)user;
    if ((pressed & NEBULA_BUTTON_MAP) != 0) {
        free_on = !free_on;
        if (free_on) {
            game->camera_position(place);
            game->camera_target(look);
        } else {
            game->set_camera(0, 0, 0.0f);
        }
    }
    if (!free_on) {
        return data->value;
    }
    {
        /* Across and along the view, on the level's floor plane. */
        float front[3] = {look[0] - place[0], 0.0f, look[2] - place[2]};
        const float step = 20.0f;
        float length = front[0] * front[0] + front[2] * front[2];
        float move[3] = {0.0f, 0.0f, 0.0f};
        if (length > 0.0f) {
            /* One Newton step from 1/length is plenty for a direction. */
            float inverse = 1.0f / (length * 0.5f + 0.5f);
            inverse = inverse * (1.5f - 0.5f * length * inverse * inverse);
            front[0] *= inverse;
            front[2] *= inverse;
        }
        if ((held & NEBULA_BUTTON_DPAD_UP) != 0) {
            move[0] += front[0] * step;
            move[2] += front[2] * step;
        }
        if ((held & NEBULA_BUTTON_DPAD_DOWN) != 0) {
            move[0] -= front[0] * step;
            move[2] -= front[2] * step;
        }
        if ((held & NEBULA_BUTTON_DPAD_LEFT) != 0) {
            move[0] += front[2] * step;
            move[2] -= front[0] * step;
        }
        if ((held & NEBULA_BUTTON_DPAD_RIGHT) != 0) {
            move[0] -= front[2] * step;
            move[2] += front[0] * step;
        }
        place[0] += move[0];
        place[2] += move[2];
        look[0] += move[0];
        look[2] += move[2];
        game->set_camera(place, look, 0.0f);
    }
    return data->value;
}

NEBULA_PLUGIN_ENTRY(api, self) {
    game = api;
    me = self;
    game->subscribe(NEBULA_MOD_EVENT_STAGE_FRAME, stage_frame, 0);
}
