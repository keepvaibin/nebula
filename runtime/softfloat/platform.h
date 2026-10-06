#pragma once

#define LITTLEENDIAN 1

#if defined(_MSC_VER)
#define INLINE static __forceinline
#define THREAD_LOCAL __declspec(thread)
#else
#define INLINE static inline
#define THREAD_LOCAL _Thread_local
#endif
