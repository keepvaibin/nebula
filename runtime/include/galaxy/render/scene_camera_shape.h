#pragma once

// The scene camera's constants block (scene_camera.hlsl, which holds its numbers), the same on every backend.

namespace galaxy::render::camera {

// The constants of both passes.
struct Constants {
    float width = 0.0f, height = 0.0f;  // the picture's pixels
    float seconds = 0.0f;               // since the last meter
    float unused = 0.0f;                // the block is one 16-byte row
};

}  // namespace galaxy::render::camera
