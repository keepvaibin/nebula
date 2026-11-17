/* lt-cxx: a C++ mod (loader contract 8, LR-6, and "Terminators and constructor order"). Globals are built by luma.h's
   begin before start, in priority order, then in source order; new and delete use the mod's memory; a virtual call
   works; at finish (after the grace) the destructors atexit took run the last first, then the terminators. The finish
   lines are logged, and also facts: 1121 proves the start order only (the run may end first, run 16, R16-4); 1143 takes
   the mod out while the game runs, so its stop (fact `stopped`), then each destructor (fact `destroyed_<id>`, its place
   in the order from 1: 4, 3, 2, 1 expected) and the terminator (fact `terminated`, 5) are seen before takeout.lt-cxx. */
#include "lt.h"

static int order_seen[8];
static int order_count;
static int destroy_count;

struct Mark {
    int id;
    explicit Mark(int i) : id(i) {
        if (order_count < 8) order_seen[order_count++] = i;
    }
    ~Mark() {
        char line[48];
        char* at = lt_put(line, "lt-cxx: destroyed ");
        at = lt_num(at, id);
        *at = 0;
        if (lt_core != nullptr) {
            lt_say(line);
            char key[24];
            char* k = lt_put(key, "destroyed_");
            k = lt_num(k, id);
            *k = 0;
            lt_fact(key, ++destroy_count);
        }
    }
};

__attribute__((init_priority(2000))) static Mark late(2);
__attribute__((init_priority(1000))) static Mark early(1);
static Mark plain(3);
static Mark plain2(4);  // after plain, also at the default priority: source order (run 16, R16-3)

struct Shape {
    virtual int sides() const = 0;
    virtual ~Shape() = default;
};
struct Square : Shape {
    int sides() const override { return 4; }
};

__attribute__((destructor)) static void terminator() {
    if (lt_core != nullptr) {
        lt_say("lt-cxx: terminator ran");
        lt_fact("terminated", ++destroy_count);
    }
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    lt_core = core;
    lt_mod = mod;
    lt_fact("order", order_seen[0] * 1000 + order_seen[1] * 100 + order_seen[2] * 10 + order_seen[3]);
    Shape* s = new Square();
    lt_fact("virtual", s->sides());
    delete s;
    int* many = new int[1000];
    for (int i = 0; i < 1000; i++) many[i] = i;
    lt_fact("array", many[999]);
    delete[] many;
    lt_fact("started", 1);
    return 0;
}

static void stop(void) {
    lt_fact("stopped", destroy_count == 0);
}

static const LumaFunction* unused;
static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerLeft", "s32()"),
};
LUMA_MOD(imports, start, stop);
