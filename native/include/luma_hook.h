#pragma once
// The row and entry check every game function carries (written into each function by tools/lumacore/instrument.py).

#if defined(_WIN32)
#define LUMA_THREAD __declspec(thread)
#else
#define LUMA_THREAD __thread
#endif

namespace luma {
    // Calls the function with args[0] as `this` (null for a function without one) and args[1..] pointing at its
    // arguments, and puts what it returns at `result`. Hooks on the function run.
    typedef void (*Invoke)(void** args, void* result);

    struct Function {
        const char* name;       // "Kuribo::exeWander"; "File.cpp#name" for a function only its file sees
        const char* signature;  // "void(f32, const TVec3f &)", as the source writes it
        Invoke invoke;
        unsigned char* hooked;
    };

    // The function whose own code the hook runner is about to enter on this thread.
    [[clang::require_constant_initialization]] extern LUMA_THREAD const Function* tBody;

    // Runs the hooks of a function whose flag is set; they or the function itself fill `result`.
    void run(const Function* fn, void** args, void* result);
    // A constructor or destructor: its hooks are told, and it goes on.
    void tell(const Function* fn, void** args);

    template <class T> using Type = T;
    template <class T> struct Bare { typedef T type; };
    template <class T> struct Bare<T&> { typedef T type; };
    template <class T> struct Bare<T&&> { typedef T type; };

    // A parameter's type as the function receives it: an array is a pointer to its first element.
    template <class T> struct Passed { typedef T type; };
    template <class T> struct Passed<T[]> { typedef T* type; };
    template <class T, decltype(sizeof(0)) N> struct Passed<T[N]> { typedef T* type; };
    template <class T> using Param = typename Passed<T>::type;

    template <class T> inline Param<T>&& arg(void* p) {
        return static_cast<Param<T>&&>(*static_cast<typename Bare<Param<T>>::type*>(p));
    }

    struct Place {};
}

inline void* operator new(decltype(sizeof(0)), luma::Place, void* p) noexcept { return p; }

namespace luma {
    template <class R> struct Result {
        alignas(R) unsigned char bytes[sizeof(R)];
        template <class F> static void put(void* r, F&& f) { ::new (Place(), r) R(f()); }
        R take() {
            R* p = reinterpret_cast<R*>(bytes);
            struct End { R* p; ~End() { p->~R(); } } end = {p};
            return static_cast<R&&>(*p);
        }
    };
    template <class R> struct Result<R&> {
        R* p;
        template <class F> static void put(void* r, F&& f) { *static_cast<R**>(r) = &f(); }
        R& take() { return *p; }
    };
    template <> struct Result<void> {
        template <class F> static void put(void*, F&& f) { f(); }
        void take() {}
    };

    template <class R> inline R dispatch(const Function* fn, void** args) {
        Result<R> result;
        run(fn, args, &result);
        return result.take();
    }
}

#if defined(_WIN32)
#define LUMA_FLAG_SECTION __attribute__((section("lumaflg$m")))
#define LUMA_ROW_SECTION __attribute__((section("lumafn$m"), used))
#elif defined(__APPLE__)
#define LUMA_FLAG_SECTION __attribute__((section("__DATA,lumaflg")))
#define LUMA_ROW_SECTION __attribute__((section("__DATA,lumafn"), used))
#else
#define LUMA_FLAG_SECTION __attribute__((section("lumaflg")))
#define LUMA_ROW_SECTION __attribute__((section("lumafn"), used, retain))
#endif

#define LUMA_LIST(...) {__VA_ARGS__}

#define LUMA_ROW(NAME, SIGNATURE, INVOKE)                   \
    LUMA_FLAG_SECTION static unsigned char luma_hooked = 0; \
    LUMA_ROW_SECTION static const ::luma::Function luma_fn = {NAME, SIGNATURE, INVOKE, &luma_hooked}

// RESULT: the function's result type. CALL: the function called on luma_a. PARAMS and ARGS: the hooked path's own
// copy of the parameters (it is another function, so the path with no hook keeps nothing for it). POINTERS: args.
#define LUMA_HOOK(NAME, SIGNATURE, RESULT, CALL, PARAMS, ARGS, POINTERS)                                             \
    LUMA_ROW(NAME, SIGNATURE, ([](void** luma_a, void* luma_r) {                                                     \
        ::luma::Result<::luma::Type<RESULT>>::put(luma_r, [&]() -> ::luma::Type<RESULT> { return CALL; });           \
    }));                                                                                                             \
    if (__builtin_expect(luma_hooked, 0)) {                                                                          \
        if (::luma::tBody == &luma_fn) {                                                                             \
            ::luma::tBody = nullptr;                                                                                 \
        } else {                                                                                                     \
            return (+[] PARAMS __attribute__((noinline, cold)) -> ::luma::Type<RESULT> {                             \
                void* luma_args[] = LUMA_LIST POINTERS;                                                                        \
                return ::luma::dispatch<::luma::Type<RESULT>>(&luma_fn, luma_args);                                  \
            }) ARGS;                                                                                                 \
        }                                                                                                            \
    }

#define LUMA_TELL(NAME, SIGNATURE, POINTERS)       \
    LUMA_ROW(NAME, SIGNATURE, nullptr);            \
    if (__builtin_expect(luma_hooked, 0)) {        \
        void* luma_args[] = LUMA_LIST POINTERS;              \
        ::luma::tell(&luma_fn, luma_args);         \
    }
