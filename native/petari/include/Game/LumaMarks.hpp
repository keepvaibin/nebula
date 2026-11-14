#pragma once

// What a function does with a pointer parameter, for LumaCore's self-description
// (docs/modding/language/game-description.md). Written before the parameter's type in a header's declaration:
// `void getPlayerFrontVec(LUMA_OUT Vec3f*);`.
// Each mark expands to nothing for every compiler: the module's code is the same with or without them.
// tools/lumacore/instrument.py reads them and gives each function row its "marks" (luma_functions.json); it stops the
// build, naming the file and line, on a mark of the wrong form. A parameter is marked only from the function's body.
//
//   LUMA_OUT             it may write one value through the pointer, never reads it first, does not keep the address
//   LUMA_IN              it reads one value through it, never writes it, does not keep it
//   LUMA_INOUT           it reads one value, may write it, does not keep it
//   LUMA_KEPT            the game keeps the address (a registration); the pointer may be to anything (a matrix, a struct)
//   LUMA_COUNT(param)    with a way: an array whose element count is the integer parameter `param`
//   LUMA_COUNT_FIXED(n)  with a way: an array of exactly n elements (1 to 4096)
//   LUMA_NULLABLE        with any way: the function accepts null there (with LUMA_KEPT: it reads the kept address only
//                        when it was given one)
//
// Except under LUMA_KEPT, the pointer is to a number, an enum or a vector (2 to 4 floats x, y, z, w).

#define LUMA_OUT
#define LUMA_IN
#define LUMA_INOUT
#define LUMA_KEPT
#define LUMA_COUNT(param)
#define LUMA_COUNT_FIXED(n)
#define LUMA_NULLABLE
