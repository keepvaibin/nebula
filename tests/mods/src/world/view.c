/* FB's draw.view conformance mods (plan 2.4 layer 7; native/include/luma_draw_view.h), one case each (-DCASE=n). Run
   steps call MR::getRandom(0, 2470 + k) and the hook gives 1. Each call's status is the fact mod.<id>.<name>.
   fv-view (1) 2471: k 1 a 256x144 view cleared magenta ("create"), its texture ("texture", "texture_same": the same
                handle again), drawn over the screen's top-left quarter at hud.after every frame ("at"; "image" the first
                frame's status): no camera yet, so black. k 2 its camera 450 along world +z from the player and 120 up
                (behind him at this start, where the game's camera is), looking at his middle, fov 50 ("camera"). k 3 set_on 0 ("off") and the camera turned to look away ("camera_off"):
                the texture keeps its picture. k 4 set_on 1 ("on"), a WORLD rig at the player, offset (0, 150, 400), fov
                50 ("rig"); eye_of that rig: "eye_dx", "eye_dy", "eye_dz" (the eye less the player's place, rounded),
                "at_d2" (|at - place| squared, rounded), "eye_status". k 5 main_eye 600 to the player's side and 200 up, looking at
                him ("main"). k 6 the game's MR::getCameraWatchPos is the point that main camera looks at ("main_watch_d2": the
                distance squared, rounded), then main_eye null ("main_back"); k 7 the watch point is the game's camera's
                again, away from that point ("back_watch_d2" over 100). k 7 release ("release"), again ("release2": E_HANDLE); the
                draw of its texture after: E_HANDLE ("image_after").
   fv-args (2) 2472: the refusals at k 1, each reported: create 0 wide ("zero": E_ARGUMENT), past 1024 ("wide"), a null
                out ("null_out"); two views ("first", "second"), a third ("third": E_LIMIT); camera at the eye
                ("at_eye"), up along the look ("up_along"), fov 1 ("fov_low"), fov 179 ("fov_high"), a NaN eye
                ("nan"); follow a null actor ("follow_null"), offset of length 0 ("follow_zero"); texture with a null
                out ("texture_null"); texture_free of the view's texture ("free_view": E_ARGUMENT); rig null ("rig_null"),
                rig 7 ("rig_unknown"), a JOINT rig on the player's joint "FvNoJoint" ("rig_joint"), a JOINT rig on
                "Head" with near 0 ("rig_near"); eye_of with a null out ("eye_null"); main_eye with a NaN ("main_nan");
                set_on and camera on handle 999 ("on_bad", "camera_bad": E_HANDLE); release both ("release_first",
                "release_second"). */
#include "../loader/lt.h"
#include <luma_draw.h>
#include <luma_draw_view.h>

#ifndef CASE
#error CASE
#endif

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static const LumaDrawService* draw;
static const LumaDrawViewService* view;
static const LumaFunction* get_player_pos; /* Vec3f* MR::getPlayerPos() */
static const LumaFunction* player_of;      /* LiveActor* MR::getPlayerDemoActor() */
static const LumaFunction* watch_pos;      /* const Vec3f& MR::getCameraWatchPos() */

__attribute__((unused)) static float watch_d2(Vec3 to) {
    const Vec3* at = 0;
    void* none[] = {0};
    watch_pos->invoke(none, &at);
    if (at == 0) return -1.0f;
    const float dx = at->x - to.x, dy = at->y - to.y, dz = at->z - to.z;
    return dx * dx + dy * dy + dz * dz;
}

static Vec3 player_pos(void) {
    const Vec3* at = 0;
    void* none[] = {0};
    get_player_pos->invoke(none, &at);
    Vec3 out = {0, 0, 0};
    if (at != 0) out = *at;
    return out;
}

static LumaActor player(void) {
    void* actor = 0;
    void* args[] = {0};
    player_of->invoke(args, &actor);
    return actor;
}

__attribute__((unused)) static long long rounded(float v) {
    return (long long)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

#if CASE == 1
static LumaDrawViewRegion region;
static LumaDrawTexture shown;
static int frames;
static int released;
static Vec3 main_at; /* the point the main camera of k 5 looks at */

static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    if (released == 2) return; /* the draw after release is checked once */
    const LumaDrawRect quarter = {0.0f, 0.0f, screen->w * 0.5f, screen->h * 0.5f};
    const int32_t s = draw->image(lt_mod, shown, &quarter, 0, RGBA(255, 255, 255, 255));
    if (frames++ == 0) lt_fact("image", s);
    if (released == 1) {
        released = 2;
        lt_fact("image_after", s);
    }
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2470;
    if (k < 1 || k > 9) return 0;
    const Vec3 p = player_pos();
#if CASE == 1
    if (k == 1) {
        lt_fact("create", view->create(lt_mod, 256, 144, RGBA(255, 0, 255, 255), &region));
        lt_fact("texture", view->texture(lt_mod, region, &shown));
        LumaDrawTexture again = 0;
        (void)view->texture(lt_mod, region, &again);
        lt_fact("texture_same", again == shown && shown != 0);
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_HUD_AFTER, paint, 0));
    }
    if (k == 2) {
        const LumaDrawVec3 eye = {p.x, p.y + 120.0f, p.z + 450.0f};
        const LumaDrawVec3 at = {p.x, p.y + 80.0f, p.z};
        const LumaDrawVec3 up = {0.0f, 1.0f, 0.0f};
        lt_fact("camera", view->camera(lt_mod, region, &eye, &at, &up, 50.0f));
    }
    if (k == 3) {
        lt_fact("off", view->set_on(lt_mod, region, 0));
        const LumaDrawVec3 eye = {p.x, p.y + 120.0f, p.z + 450.0f};
        const LumaDrawVec3 at = {p.x, p.y + 120.0f, p.z + 900.0f};
        const LumaDrawVec3 up = {0.0f, 1.0f, 0.0f};
        lt_fact("camera_off", view->camera(lt_mod, region, &eye, &at, &up, 50.0f));
    }
    if (k == 4) {
        lt_fact("on", view->set_on(lt_mod, region, 1));
        LumaDrawViewCamera rig = {.rig = LUMA_DRAW_VIEW_RIG_WORLD, .fov_y = 50.0f, .actor = player(),
                                  .offset = {0.0f, 150.0f, 400.0f}};
        lt_fact("rig", view->rig(lt_mod, region, &rig));
        LumaDrawViewEye eye = {.fov_y = 0.0f};
        lt_fact("eye_status", view->eye_of(lt_mod, &rig, &eye));
        lt_fact("eye_dx", rounded(eye.eye[0] - p.x));
        lt_fact("eye_dy", rounded(eye.eye[1] - p.y));
        lt_fact("eye_dz", rounded(eye.eye[2] - p.z));
        const float dx = eye.at[0] - p.x, dy = eye.at[1] - p.y, dz = eye.at[2] - p.z;
        lt_fact("at_d2", rounded(dx * dx + dy * dy + dz * dz));
        lt_fact("eye_fov", rounded(eye.fov_y));
    }
    if (k == 5) {
        const LumaDrawViewEye eye = {{p.x + 600.0f, p.y + 200.0f, p.z}, {p.x, p.y + 80.0f, p.z}, {0.0f, 1.0f, 0.0f}, 50.0f};
        main_at = (Vec3){eye.at[0], eye.at[1], eye.at[2]};
        lt_fact("main", view->main_eye(lt_mod, &eye));
    }
    if (k == 6) {
        lt_fact("main_watch_d2", rounded(watch_d2(main_at)));
        lt_fact("main_back", view->main_eye(lt_mod, 0));
    }
    if (k == 7) {
        lt_fact("back_watch_d2", rounded(watch_d2(main_at)));
        lt_fact("release", view->release(lt_mod, region));
        lt_fact("release2", view->release(lt_mod, region));
        released = 1;
    }
#elif CASE == 2
    if (k == 1) {
        LumaDrawViewRegion first = 0, second = 0, third = 0, none = 0;
        lt_fact("zero", view->create(lt_mod, 0, 64, RGBA(0, 0, 0, 255), &none));
        lt_fact("wide", view->create(lt_mod, LUMA_DRAW_VIEW_SIDE_MOST + 1u, 64, RGBA(0, 0, 0, 255), &none));
        lt_fact("null_out", view->create(lt_mod, 64, 64, RGBA(0, 0, 0, 255), 0));
        lt_fact("first", view->create(lt_mod, 64, 64, RGBA(0, 0, 0, 255), &first));
        lt_fact("second", view->create(lt_mod, 64, 64, RGBA(0, 0, 0, 255), &second));
        lt_fact("third", view->create(lt_mod, 64, 64, RGBA(0, 0, 0, 255), &third));
        const LumaDrawVec3 eye = {p.x, p.y + 100.0f, p.z + 300.0f};
        const LumaDrawVec3 at = {p.x, p.y + 100.0f, p.z};
        const LumaDrawVec3 up = {0.0f, 1.0f, 0.0f};
        const LumaDrawVec3 along = {0.0f, 0.0f, -2.0f};
        const float zero = 0.0f;
        const LumaDrawVec3 nan_eye = {zero / zero, p.y, p.z};
        lt_fact("at_eye", view->camera(lt_mod, first, &eye, &eye, &up, 50.0f));
        lt_fact("up_along", view->camera(lt_mod, first, &eye, &at, &along, 50.0f));
        lt_fact("fov_low", view->camera(lt_mod, first, &eye, &at, &up, LUMA_DRAW_VIEW_FOV_LEAST));
        lt_fact("fov_high", view->camera(lt_mod, first, &eye, &at, &up, LUMA_DRAW_VIEW_FOV_MOST));
        lt_fact("nan", view->camera(lt_mod, first, &nan_eye, &at, &up, 50.0f));
        const LumaDrawVec3 offset = {0.0f, 100.0f, 300.0f};
        const LumaDrawVec3 nowhere = {0.0f, 0.0f, 0.0f};
        lt_fact("follow_null", view->follow(lt_mod, first, 0, &offset, 50.0f));
        lt_fact("follow_zero", view->follow(lt_mod, first, player(), &nowhere, 50.0f));
        lt_fact("texture_null", view->texture(lt_mod, first, 0));
        LumaDrawTexture its = 0;
        (void)view->texture(lt_mod, first, &its);
        lt_fact("free_view", draw->texture_free(lt_mod, its));
        lt_fact("rig_null", view->rig(lt_mod, first, 0));
        LumaDrawViewCamera unknown = {.rig = (LumaDrawViewRig)7, .fov_y = 50.0f, .actor = player()};
        lt_fact("rig_unknown", view->rig(lt_mod, first, &unknown));
        LumaDrawViewCamera joint = {.rig = LUMA_DRAW_VIEW_RIG_JOINT, .fov_y = 50.0f, .actor = player(),
                                    .joint = "FvNoJoint", .near_z = 10.0f};
        lt_fact("rig_joint", view->rig(lt_mod, first, &joint));
        LumaDrawViewCamera near0 = {.rig = LUMA_DRAW_VIEW_RIG_JOINT, .fov_y = 50.0f, .actor = player(), .joint = "Head"};
        lt_fact("rig_near", view->rig(lt_mod, first, &near0));
        LumaDrawViewCamera fixed = {.rig = LUMA_DRAW_VIEW_RIG_FIXED, .eye = {p.x, p.y + 100.0f, p.z + 300.0f},
                                    .at = {p.x, p.y + 100.0f, p.z}, .up = {0.0f, 1.0f, 0.0f}, .fov_y = 50.0f};
        lt_fact("eye_null", view->eye_of(lt_mod, &fixed, 0));
        const LumaDrawViewEye nan_main = {{zero / zero, p.y, p.z}, {p.x, p.y, p.z}, {0.0f, 1.0f, 0.0f}, 50.0f};
        lt_fact("main_nan", view->main_eye(lt_mod, &nan_main));
        lt_fact("on_bad", view->set_on(lt_mod, 999, 0));
        lt_fact("camera_bad", view->camera(lt_mod, 999, &eye, &at, &up, 50.0f));
        lt_fact("release_first", view->release(lt_mod, first));
        lt_fact("release_second", view->release(lt_mod, second));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(draw, LUMA_DRAW_NAME, "1.0"),
    LUMA_SERVICE(view, LUMA_DRAW_VIEW_NAME, "1.0"),
    LUMA_FUNCTION(player_of, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_FUNCTION(get_player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(watch_pos, "MR::getCameraWatchPos", "const Vec3f &()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
