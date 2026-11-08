#include "Game/Ui/ResourceAccessor.hpp"

namespace Ui {
    ResourceAccessor::~ResourceAccessor() {
    }

    ResourceAccessor::ResourceAccessor() {
    }

    Font* ResourceAccessor::GetFont(const char*) {
        return nullptr;
    }
}  // namespace Ui
