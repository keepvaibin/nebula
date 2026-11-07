#pragma once


class TableIter;
class LiveActor;

namespace MR {
    class FunctorBase;
};  // namespace MR

// The functions are defined in namespace AstroDemoFunction (Game/Demo/AstroDemoFunction.cpp); the MSVC ABI mangles
// a class's static members apart from a namespace's functions. No caller uses the results.
#include "Game/Demo/AstroDemoFunction.hpp"
