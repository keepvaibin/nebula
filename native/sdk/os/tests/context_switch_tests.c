/* nebula_switch_stack and nebula_prepare_stack: threads on prepared stacks run, switch back and forth, keep their
   callee-saved registers and floating-point state, and see 16-byte-aligned stacks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void nebula_switch_stack(void** from_sp, void* to_sp);
void* nebula_prepare_stack(void* top, size_t size, void (*entry)(void*), void* arg);

static void* sMainSp;
static void* sSp[2];
static int sFailures;
static volatile double sSink;

void nebula_thread_start(void (*entry)(void*), void* arg) {
    entry(arg);
    printf("FAIL thread returned past its entry\n");
    exit(1);
}

static void check_alignment(const char* where) {
    double probe __attribute__((aligned(16)));
    if (((uintptr_t)&probe & 15) != 0) {
        printf("FAIL stack misaligned in %s\n", where);
        sFailures++;
    }
}

static void worker(void* arg) {
    const int id = (int)(intptr_t)arg;
    double acc = id + 0.5;
    check_alignment(id ? "thread 1" : "thread 0");
    for (int round = 0; round < 1000; round++) {
        acc = acc * 1.0000001 + id;
        nebula_switch_stack(&sSp[id], id == 0 ? sSp[1] : sMainSp);
    }
    sSink = acc;
    printf("thread %d done (%g)\n", id, acc);
    nebula_switch_stack(&sSp[id], sMainSp);
}

int main(void) {
    enum { kStack = 256 * 1024 };
    char* stacks = (char*)malloc(2 * kStack);
    double before = 3.25;
    sSp[0] = nebula_prepare_stack(stacks + kStack, kStack, worker, (void*)(intptr_t)0);
    sSp[1] = nebula_prepare_stack(stacks + 2 * kStack, kStack, worker, (void*)(intptr_t)1);
    /* main -> 0 -> 1 -> main, 1000 times. */
    for (int round = 0; round < 1000; round++) {
        nebula_switch_stack(&sMainSp, sSp[0]);
        before += 1.0;
    }
    nebula_switch_stack(&sMainSp, sSp[0]);
    nebula_switch_stack(&sMainSp, sSp[1]);
    if (before != 1003.25) {
        printf("FAIL main's floating-point state changed (%g)\n", before);
        sFailures++;
    }
    printf(sFailures ? "%d failures\n" : "all passed\n", sFailures);
    return sFailures != 0;
}
