#pragma once

// The settings' per-file save import (nebula_save_import.h): the host's read of a save, answered on the game thread
// once the game reads and writes no save.
namespace NebulaSaveImport {
    void update();  // GameSystem::update, once a frame: one load while nothing waits
};  // namespace NebulaSaveImport
