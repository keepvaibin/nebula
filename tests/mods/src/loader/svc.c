/* Services as imports (loader D3-2, LUMA_SERVICE):
   lt-svc      (-DCASE=1): the harness's service "harness" 1.0, present: the slot is filled before start.
   lt-svc-none (-DCASE=2): "lt.absent" 1.0 (a name in registry 2's grammar), required: refused, L505 Name.Service, the mod off.
   lt-svc-opt  (-DCASE=3): the same, LUMA_OPTIONAL: on, the slot left null.
   lt-svc-new  (-DCASE=4): "harness" 1.99 (a minor this game does not have): refused like an absent one.
   Each also asks core->service at start for the same name, which must agree with the slot. */
#include "lt.h"
#include <luma_harness.h>

#ifndef CASE
#error CASE
#endif

static const void* table;

#if CASE == 1
#define NAME LUMA_HARNESS_NAME
#define VERSION "1.0"
#define FLAGS 0u
#elif CASE == 2
#define NAME "lt.absent"
#define VERSION "1.0"
#define FLAGS 0u
#elif CASE == 3
#define NAME "lt.absent"
#define VERSION "1.0"
#define FLAGS LUMA_OPTIONAL
#else
#define NAME LUMA_HARNESS_NAME
#define VERSION "1.99"
#define FLAGS 0u
#endif

static const LumaImport imports[] = {
    {LUMA_IMPORT_SERVICE, FLAGS, NAME, VERSION, &table, 0, 0, 0},
};

LT_START({
    lt_fact("slot", table != 0);
    const void* asked = core->service(mod, NAME, 1, CASE == 4 ? 99 : 0);
    lt_fact("agrees", asked == table);
})
LUMA_MOD(imports, start, 0);
