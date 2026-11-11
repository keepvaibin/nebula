/* A mod library's C++ global constructors, kept from the system's loader (with -fno-use-init-array) and run by
   luma.h's begin after the core is known (loader contract 8, LR-6). Linux and Android: -Wl,-T,luma_ctors.ld. */
SECTIONS { luma_ctors : { KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*))) KEEP(*(.ctors)) } } INSERT AFTER .data;
