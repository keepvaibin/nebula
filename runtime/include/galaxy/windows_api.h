// Windows.h for Nebula's own code, the one place its options are set: lean, and without the min/max macros. Every
// file of ours includes this instead of <Windows.h> (and before a system header that pulls Windows.h in, such as
// d3d12.h); third-party code keeps its own. Guarded: the Vulkan targets set both too (cmake/vulkan_deps.cmake), since
// vulkan.h includes Windows.h itself.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
