/* A mod library's C++ global constructors, kept from the system's loader (with -fno-use-init-array) and run by
   luma.h's begin after the core is known (docs/modding/lumalink/loader.md), and its terminators (`.dtors`: a destructor
   attribute's function not registered with atexit), which no system loader runs, run by luma.h's finish.
   Linux and Android: -Wl,-T,luma_ctors.ld. Each section is always made (a null entry first, which the runners
   skip), so its bounds always resolve. The layout is GNU ld's own for .ctors/.dtors: the plain sections, then
   the priority sections by name (the compiler names them 65535 - priority, five digits), so luma.h's runners
   (constructors from the last, terminators from the first) give priority 101 first and its terminator last. */
SECTIONS {
    luma_ctors : { QUAD(0) KEEP(*(.ctors)) KEEP(*(SORT_BY_NAME(.ctors.*))) }
    luma_dtors : { QUAD(0) KEEP(*(.dtors)) KEEP(*(SORT_BY_NAME(.dtors.*))) }
} INSERT AFTER .data;
