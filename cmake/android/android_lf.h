/* libarchive's archive.h and archive_entry.h include this header when built for Android (its own copy lives in
 * contrib/android, which third_party/archive leaves out). Its job there is mapping file calls to their 64-bit forms on
 * 32-bit ABIs. Nebula builds Android for 64-bit ABIs only (arm64-v8a, x86_64), where off_t and the file calls are 64-bit
 * already, so there is nothing to map. */
#ifndef NEBULA_ANDROID_LF_H
#define NEBULA_ANDROID_LF_H
#if !defined(__LP64__)
#error "Nebula's Android build is 64-bit only; a 32-bit ABI needs libarchive's contrib/android/include/android_lf.h"
#endif
#endif
