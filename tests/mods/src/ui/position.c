/* ju-position (3814, ui 1.3 set_position): its own layout JuStack (position/LayoutData/JuStack.arc, cooked at install)
   holds a pane Stack at -150,0 with two copies of one button, A (red) and B (blue), both at 40,20 in Stack's space.
   Run steps call MR::getRandom(0, 3800 + k) and the hook gives 1.
   k 1: open ("open"), panes A and B ("pane_a", "pane_b"), A placed at 0,60 and B at 0,-60 ("move_a", "move_b"):
        the places replace the layout's 40,20, so A stands 60 up and B 60 down from Stack's origin, one above the
        other. The test's picture comes before k 2: the mod log's notices of k 2's refusals cover B's place.
   k 2: refused, each LUMA_E_ARGUMENT and nothing moved: a NaN x ("nan"), y 5000 ("far"), an infinite y ("inf"),
        x -4097 ("far_neg"); 4096 either way is taken ("edge") and A put back at 0,60 ("back"). A handle never given:
        LUMA_E_HANDLE ("unknown").
   k 3: close ("close"), set_position on the closed screen's pane: LUMA_E_HANDLE ("closed"). */
#include "../loader/lt.h"
#include <luma_ui.h>

static const LumaUiService* ui;
static LumaUiScreen screen;
static LumaUiPane a;
static LumaUiPane b;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3800;
    if (k < 1 || k > 3) return 0;
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "JuStack", LUMA_UI_HUD, &screen));
        lt_fact("pane_a", ui->pane(lt_mod, screen, "A", &a));
        lt_fact("pane_b", ui->pane(lt_mod, screen, "B", &b));
        lt_fact("move_a", ui->set_position(lt_mod, a, 0.0f, 60.0f));
        lt_fact("move_b", ui->set_position(lt_mod, b, 0.0f, -60.0f));
    }
    if (k == 2) {
        lt_fact("nan", ui->set_position(lt_mod, a, __builtin_nanf(""), 0.0f));
        lt_fact("far", ui->set_position(lt_mod, a, 0.0f, 5000.0f));
        lt_fact("inf", ui->set_position(lt_mod, a, 0.0f, __builtin_inff()));
        lt_fact("far_neg", ui->set_position(lt_mod, a, -4097.0f, 0.0f));
        lt_fact("edge", ui->set_position(lt_mod, a, 4096.0f, -4096.0f));
        lt_fact("back", ui->set_position(lt_mod, a, 0.0f, 60.0f));
        lt_fact("unknown", ui->set_position(lt_mod, 999999, 0.0f, 0.0f));
    }
    if (k == 3) {
        lt_fact("close", ui->close(lt_mod, screen));
        lt_fact("closed", ui->set_position(lt_mod, a, 0.0f, 0.0f));
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.3"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
