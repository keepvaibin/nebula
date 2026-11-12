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
    // Given as `args` to a function's invoke, it asks instead of calling: the answer (an unsigned at `result`) is the
    // kind of the function's result, Result<R>::kKind (loader contract 4, LR-1).
    extern void* kAskKind[1];
    enum : unsigned {
        kPlain = 0,      // zeroed, then default-constructed before the chain: a skip or a wrap that does not proceed
                         // gives the caller that
        kReference = 1,  // the slot is a pointer, null before the chain: still null after it, the kernel runs the function
        kFixed = 2       // a class with no default constructor, or with a destructor: no wrap or replace, no skip
    };

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
    // Where a hooked call's result goes (loader contract 4, Z-2 and LR-1). A plain result holds R's default before any
    // hook runs (zero for a scalar or a plain struct), so a before that skips without writing it, or a wrap that does
    // not proceed, gives the caller that, never uninitialised bytes. A reference is a null pointer until written; the
    // kernel runs the function when the chain leaves it null. A fixed result (a class with no default constructor, or
    // with a destructor, which the function's own value is built over) takes no wrap or replace and no skip.
    template <class R> struct Result {
        static constexpr unsigned kKind = __is_constructible(R) && __is_trivially_destructible(R) ? kPlain : kFixed;
        alignas(R) unsigned char bytes[sizeof(R)];
        Result() {
            if constexpr (kKind == kPlain) {
                // Zero first: a class whose default constructor sets nothing (TVec3f's) still reads 0, 0, 0.
                __builtin_memset(bytes, 0, sizeof(bytes));
                ::new (Place(), bytes) R();
            }
        }
        template <class F> static void put(void* r, F&& f) { ::new (Place(), r) R(f()); }
        R take() {
            R* p = reinterpret_cast<R*>(bytes);
            struct End { R* p; ~End() { p->~R(); } } end = {p};
            return static_cast<R&&>(*p);
        }
    };
    template <class R> struct Result<R&> {
        static constexpr unsigned kKind = kReference;
        R* p = nullptr;
        template <class F> static void put(void* r, F&& f) { *static_cast<R**>(r) = &f(); }
        R& take() { return *p; }
    };
    template <> struct Result<void> {
        static constexpr unsigned kKind = kPlain;
        template <class F> static void put(void*, F&& f) { f(); }
        void take() {}
    };

    // A number written in a function's code, or a file's number constant (1.5; instrument.py names them): the
    // function's row name and signature and the number's place in its code (from 1), or the constant's name, "" and 0.
    // kind: 1 f32, 2 f64, 3 s8, 4 u8, 5 s16, 6 u16, 7 s32, 8 u32, 9 s64, 10 u64.
    struct Number {
        const char* name;
        const char* signature;
        unsigned place;
        unsigned kind;
        void* address;
    };
    // One unit's numbers, in the "lumanum" section. 32 bytes aligned to 32, so a linker's pad between objects is whole
    // records (with a null unit).
    struct alignas(32) Numbers {
        const char* unit;  // the source's path from the game's source root
        const Number* numbers;
        unsigned count;
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
#define LUMA_NUMBERS_SECTION __attribute__((section("lumanum$m"), used))
#elif defined(__APPLE__)
#define LUMA_FLAG_SECTION __attribute__((section("__DATA,lumaflg")))
#define LUMA_ROW_SECTION __attribute__((section("__DATA,lumafn"), used))
#define LUMA_NUMBERS_SECTION __attribute__((section("__DATA,lumanum"), used))
#else
#define LUMA_FLAG_SECTION __attribute__((section("lumaflg")))
#define LUMA_ROW_SECTION __attribute__((section("lumafn"), used, retain))
#define LUMA_NUMBERS_SECTION __attribute__((section("lumanum"), used, retain))
#endif

#define LUMA_LIST(...) {__VA_ARGS__}

#define LUMA_ROW(NAME, SIGNATURE, INVOKE)                   \
    LUMA_FLAG_SECTION static unsigned char luma_hooked = 0; \
    LUMA_ROW_SECTION static const ::luma::Function luma_fn = {NAME, SIGNATURE, INVOKE, &luma_hooked}

// RESULT: the function's result type. CALL: the function called on luma_a. PARAMS and ARGS: the hooked path's own
// copy of the parameters (it is another function, so the path with no hook keeps nothing for it). POINTERS: args.
#define LUMA_HOOK(NAME, SIGNATURE, RESULT, CALL, PARAMS, ARGS, POINTERS)                                             \
    LUMA_ROW(NAME, SIGNATURE, ([](void** luma_a, void* luma_r) {                                                     \
        if (__builtin_expect(luma_a == ::luma::kAskKind, 0)) {                                                       \
            *static_cast<unsigned*>(luma_r) = ::luma::Result<::luma::Type<RESULT>>::kKind;                           \
            return;                                                                                                  \
        }                                                                                                            \
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
