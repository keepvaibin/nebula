#include "Game/System/LuigiLeftSupplier.hpp"
#include "Game/System/GameDataFunction.hpp"

LuigiLeftSupplier::LuigiLeftSupplier() : mIsLuigiLeft(false) {
}

void LuigiLeftSupplier::syncWithFlags() {
    if (!GameDataFunction::isDataMario()) {
        mIsLuigiLeft = true;
    } else {
        mIsLuigiLeft = false;
    }
}
