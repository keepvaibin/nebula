/* The header's layout, as tests/validate_c.rs checks the Rust side's: compiled with -fsyntax-only for each 64-bit
   target the game ships (tests/validate_c.rs names the command). */
#include <stddef.h>
#include "../include/luma_validate.h"

_Static_assert(sizeof(LumaGameStore) == 40, "LumaGameStore");
_Static_assert(offsetof(LumaGameStore, context) == 8, "LumaGameStore.context");
_Static_assert(offsetof(LumaGameStore, folder) == 32, "LumaGameStore.folder");
_Static_assert(sizeof(LumaInstalledMod) == 32, "LumaInstalledMod");
_Static_assert(sizeof(LumaOption) == 16, "LumaOption");
_Static_assert(sizeof(LumaValidateInput) == 240, "LumaValidateInput");
_Static_assert(offsetof(LumaValidateInput, test_services) == 224, "test_services");
_Static_assert(offsetof(LumaValidateInput, test_service_count) == 232, "test_service_count");
_Static_assert(sizeof(LumaTestService) == 24, "LumaTestService");
_Static_assert(offsetof(LumaTestService, minor) == 10, "minor");
_Static_assert(offsetof(LumaTestService, permission) == 16, "permission");
_Static_assert(offsetof(LumaValidateInput, placement) == 208, "placement");
_Static_assert(offsetof(LumaValidateInput, placement_size) == 216, "placement_size");
_Static_assert(offsetof(LumaValidateInput, types_size) == 168, "types_size");
_Static_assert(offsetof(LumaValidateInput, renderer_size) == 184, "renderer_size");
_Static_assert(offsetof(LumaValidateInput, device_feature_count) == 200, "device_feature_count");
_Static_assert(offsetof(LumaValidateInput, names_size) == 64, "names_size");
_Static_assert(offsetof(LumaValidateInput, store) == 96, "store");
_Static_assert(offsetof(LumaValidateInput, position) == 124, "position");
_Static_assert(offsetof(LumaValidateInput, kept_size) == 152, "kept_size");
_Static_assert(sizeof(LumaFinding) == 48, "LumaFinding");
_Static_assert(offsetof(LumaFinding, text) == 40, "LumaFinding.text");
