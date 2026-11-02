#pragma once


namespace nw4r {
    namespace db {
        void Panic(const char*, int, const char*, ...) ATTRIBUTE_PRINTF(3, 4);
    }
}  // namespace nw4r

#ifdef NW4R_DEBUG
#define NW4R_ASSERTMSG_AT(line, exp, ...) ((void)((exp) || (nw4r::db::Panic(__FILE__, line, __VA_ARGS__), 0)))
#else
#define NW4R_ASSERTMSG_AT(line, exp, ...) ((void)0)
#endif

// Any non-null pointer.
#define NW4R_POINTER_ASSERT_AT(line, exp)                                                                                                            \
    NW4R_ASSERTMSG_AT(line, reinterpret_cast< const void* >(exp) != NULL, "NW4R:Pointer Error\n" #exp "(=%p) is not valid pointer.", (exp))

#define NW4R_ASSERT_AT(line, exp) NW4R_ASSERTMSG_AT(line, (exp), "NW4R:Failed assertion " #exp)
#define NW4R_MIN_ASSERT_AT(line, exp, min)                                                                                                           \
    NW4R_ASSERTMSG_AT(line, (exp) >= (min), #exp " is out of bounds(%d)\n%d <= " #exp " not satisfied.", static_cast< int >(exp),                    \
                      static_cast< int >(min))
#define NW4R_ALIGN2_ASSERT_AT(line, exp)                                                                                                             \
    NW4R_ASSERTMSG_AT(line, (reinterpret_cast< uintptr_t >(reinterpret_cast< const void* >(exp)) & 1) == 0,                                          \
                      "NW4R:Alignment Error(0x%x)\n" #exp " must be aligned to 2 bytes boundary.", (exp))

#define NW4R_REFERENCE_ASSERT_AT(line, ref) NW4R_POINTER_ASSERT_AT(line, &ref)

#define NW4R_NULL_ASSERT_AT(line, exp) NW4R_ASSERTMSG_AT(line, (exp) != NULL, "NW4R:Pointer must not be NULL (" #exp ")")
