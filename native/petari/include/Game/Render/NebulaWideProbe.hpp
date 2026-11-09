#pragma once

// A development check that a texture list of more than sixteen maps is read whole (GALAXY_TEST_WIDE_TABLES=1, NEBULA_DEV).
namespace NebulaWideProbe {
    // Called at the end of the scene's 3D drawing: draws the probe and reads back what an earlier frame's probe found.
    void draw();
}  // namespace NebulaWideProbe
