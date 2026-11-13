/* What a Windows image needs without a C runtime: the list of static constructors and the compiler's small helpers. A frame over a
   page would need a stack probe, which is not here: the link fails on one. */
int _fltused = 1;

/* RTTI data (the module with rows, contract objects): each class's type descriptor starts with type_info's vtable, which
   the C++ runtime would define. Nothing calls through it: the kernel compares the descriptors' addresses. */
const void* const nebula_type_info_vtable[1] __asm__("??_7type_info@@6B@") = {0};

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

/* Thread-local storage: the directory Windows reads to give each thread its copy of the image's thread variables.
   Windows gives a library loaded at run time its thread storage (writes its index to _tls_index, makes each thread's
   copy) only when the image has imports: with none it left _tls_index unwritten, so the module's thread variables lay
   in the slot of index 0, the host executable's own, and each side overwrote the other's (rootcause/1.6-guard.md).
   One import keeps the module in that pass. The index starts at one no slot has: a loader that still gives none ends
   the game at the module's first thread variable, never in another image's storage. */
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
void* const nebula_loader_import = (void*)&GetCurrentThreadId;
unsigned long _tls_index = 0xFFFFFFFFul;
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
