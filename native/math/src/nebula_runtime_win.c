/* What a Windows image needs without a C runtime: the list of static constructors and the compiler's small helpers. A frame over a
   page would need a stack probe, which is not here: the link fails on one. */
int _fltused = 1;

void _purecall(void) {
    __builtin_trap();
}

/* Static constructors: the compiler lists them between these two in the sections it sorts by name. */
typedef void (*Constructor)(void);
__attribute__((section(".CRT$XCA"), used)) static Constructor sConstructorsBegin = 0;
__attribute__((section(".CRT$XCZ"), used)) static Constructor sConstructorsEnd = 0;

void nebula_run_constructors(void) {
    for (Constructor* p = &sConstructorsBegin + 1; p < &sConstructorsEnd; ++p) {
        if (*p != 0) {
            (*p)();
        }
    }
}

/* Thread-local storage: the directory Windows reads to give each thread its copy of the image's thread variables. */
unsigned long _tls_index = 0;
__attribute__((section(".tls"))) char _tls_start = 0;
__attribute__((section(".tls$ZZZ"))) char _tls_end = 0;
__attribute__((section(".CRT$XLA"), used)) static void* const sThreadCallbacksBegin = 0;
__attribute__((section(".CRT$XLZ"), used)) static void* const sThreadCallbacksEnd = 0;

struct ThreadStorageDirectory {
    void* start;
    void* end;
    void* index;
    void* callbacks;
    unsigned long zero_fill;
    unsigned long characteristics;
};
const struct ThreadStorageDirectory _tls_used = {&_tls_start, &_tls_end, &_tls_index, (void*)(&sThreadCallbacksBegin + 1), 0, 0};
