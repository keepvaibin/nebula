/* MSL's <locale.h>, the part the game reads: the "C" locale's character maps (msl_runtime.c). */
#ifndef NEBULA_MSL_LOCALE_H
#define NEBULA_MSL_LOCALE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct _loc_ctype_cmpt {
    char name[8];
    const unsigned short* ctype_map_ptr;
    const unsigned char* upper_map_ptr;
    const unsigned char* lower_map_ptr;
    const unsigned short* wctype_map_ptr;
    const wchar_t* wupper_map_ptr;
    const wchar_t* wlower_map_ptr;
    int (*decode_mb)(wchar_t*, const char*, size_t);
    int (*encode_wc)(char*, wchar_t);
};
struct __locale {
    struct __locale* next_locale;
    char name[0x30];
    struct _loc_coll_cmpt* coll_cmpt_ptr;
    struct _loc_ctype_cmpt* ctype_cmpt_ptr;
    struct _loc_mon_cmpt* mon_cmpt_ptr;
    struct _loc_num_cmpt* num_cmpt_ptr;
    struct _loc_time_cmpt* time_cmpt_ptr;
};
extern struct __locale _current_locale;
#ifdef __cplusplus
}
#endif
#endif
