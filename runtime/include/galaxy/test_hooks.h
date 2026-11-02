// A test switch's environment variable: its name is not in a release build (NEBULA_RELEASE), so nothing can turn it on and
// tools/release_check.ps1 finds no test hook.
#pragma once

#if defined(NEBULA_RELEASE)
#define NEBULA_TEST_ENV(name) ""
#define NEBULA_TEST_ENV_W(name) L""
#else
#define NEBULA_TEST_ENV(name) name
#define NEBULA_TEST_ENV_W(name) L##name
#endif
