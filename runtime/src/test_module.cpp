#include "galaxy/native_api.h"

#include <cstring>

namespace {

const galaxy::NativeServicesV1* g_services = nullptr;

void test_entry(
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1*,
    const galaxy::NativeServicesV1*) {
    context->gpr[3] = 0x47525831;  // "GRX1"
    context->pc = 0x80004004;
}

galaxy::GameModuleManifestV1 make_manifest() {
    galaxy::GameModuleManifestV1 manifest{};
    std::memcpy(manifest.game_id, "TEST01", 7);
    std::memcpy(
        manifest.main_dol_sha1,
        "0000000000000000000000000000000000000000",
        41);
    manifest.guest_entry_point = 0x80004000;
    manifest.translated_function_count = 1;
    manifest.translated_instruction_count = 1;
    return manifest;
}

const galaxy::GameModuleManifestV1 kManifest = make_manifest();

}  // namespace

extern "C" const galaxy::GameModuleManifestV1* galaxy_module_manifest() {
    return &kManifest;
}

extern "C" bool galaxy_module_init(const galaxy::NativeServicesV1* services) {
    if (services == nullptr || services->abi_version != galaxy::kNativeAbiVersion ||
        services->struct_size < sizeof(galaxy::NativeServicesV1)) {
        return false;
    }
    g_services = services;
    if (g_services->log != nullptr) {
        g_services->log(g_services->user, galaxy::LogLevelV1::Info, "native test module initialized");
    }
    return true;
}

extern "C" void galaxy_module_entry(
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    test_entry(context, memory, g_services);
}

extern "C" galaxy::NativeGameFunction galaxy_lookup_function(std::uint32_t guest_address) {
    return guest_address == kManifest.guest_entry_point ? &test_entry : nullptr;
}

