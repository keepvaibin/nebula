// A test switch's environment variable: its name is not in a release build (NEBULA_RELEASE), so nothing can turn it on and
// tools/release_check.ps1 finds no test hook.
#pragma once

#include <cstdlib>

#if defined(NEBULA_RELEASE)
#define NEBULA_TEST_ENV(name) ""
#define NEBULA_TEST_ENV_W(name) L""
inline const char* nebula_test_env_absent() noexcept {
    return nullptr;
}
#define NEBULA_TEST_GETENV(name) nebula_test_env_absent()
#else
#define NEBULA_TEST_ENV(name) name
#define NEBULA_TEST_ENV_W(name) L##name
#define NEBULA_TEST_GETENV(name) std::getenv(name)
#endif
