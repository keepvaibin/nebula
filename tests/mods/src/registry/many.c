/* 2118-many-services (N-3008; services-registry 2). The test sets GALAXY_TEST_EXTRA_SERVICES=200: the harness gives
   test.extra.n0 to test.extra.n199 beside test.svc and test.secret, so the kernel holds the catalog's services and over
   two hundred more (it once stopped the game past 64). reg-many imports test.extra.n199 and test.svc; at its start
   "last" is the number in test.extra.n199's table (199), "middle" test.extra.n100's asked by name (100), "first"
   test.extra.n0's (0), "echo" test.svc's call (LUMA_OK) and "out" its answer (42). */
#include "../loader/lt.h"
#include "../../../../tools/test_driver/extension/luma_test_services.h"

static const LumaTestExtraService* last;
static const LumaTestSvcService* svc;

static uint32_t number_of(const LumaCore* core, const LumaMod* mod, const char* name) {
    const LumaTestExtraService* one = (const LumaTestExtraService*)core->service(mod, name, 1, 0);
    return one != 0 ? one->number : 0xffffffffu;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(last, "test.extra.n199", "1.0"),
    LUMA_SERVICE(svc, "test.svc", "1.3"),
};

LT_START({
    uint64_t out = 0;
    lt_fact("last", last->number);
    lt_fact("middle", number_of(core, mod, "test.extra.n100"));
    lt_fact("first", number_of(core, mod, "test.extra.n0"));
    lt_fact("echo", svc->echo(mod, 41, &out));
    lt_fact("out", (long long)out);
})
LUMA_MOD(imports, start, 0);
