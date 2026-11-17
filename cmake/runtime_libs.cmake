# The runtime's small libraries every desktop build and the unit tests share: atomic files, the asset store, saves, Mii
# files, save transfer, host extensions, the runtime settings, pointer events, native input and the audio mixer.
# Included by the Windows build (the root CMakeLists.txt) and by the unit tests' own project (tests/unit), which builds
# them on Linux and the Mac. A build that includes it has BoringSSL and zlib's inflate (third_party/boringssl.cmake,
# third_party/zlib_inflate.cmake) for galaxy_save_transfer. galaxy_host_options, when the including build has it, adds
# the host's own compile options.
include_guard(GLOBAL)

get_filename_component(galaxy_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(galaxy_rt "${galaxy_root}/runtime/src")

function(galaxy_runtime_lib_options target)
    if(COMMAND galaxy_host_options)
        galaxy_host_options(${target})
    endif()
endfunction()

add_library(galaxy_runtime_abi INTERFACE)
target_include_directories(galaxy_runtime_abi INTERFACE "${galaxy_root}/runtime/include" "${galaxy_root}/native/include")
target_compile_features(galaxy_runtime_abi INTERFACE cxx_std_20)

add_library(galaxy_atomic_file STATIC ${galaxy_rt}/atomic_file.cpp)
target_link_libraries(galaxy_atomic_file PUBLIC galaxy_runtime_abi)

add_library(galaxy_asset_store STATIC ${galaxy_rt}/asset_store.cpp)
target_link_libraries(galaxy_asset_store PUBLIC galaxy_runtime_abi)

add_library(galaxy_save_files STATIC ${galaxy_rt}/save_files.cpp)
target_link_libraries(galaxy_save_files PUBLIC galaxy_runtime_abi galaxy_atomic_file)

# Every console's Mii files (mii_formats.h): the Mii import's reader, and where a Wii save folder keeps its Mii database.
add_library(galaxy_mii_formats STATIC ${galaxy_rt}/mii_formats.cpp)
target_link_libraries(galaxy_mii_formats PUBLIC galaxy_runtime_abi)
galaxy_runtime_lib_options(galaxy_mii_formats)

# Save import and export in the game (save_transfer.h): its zips, the Wii SD save's AES and the GameData.bin check are
# BoringSSL's, zlib's inflate and the game's own SaveFileFormat.hpp.
add_library(galaxy_save_transfer STATIC ${galaxy_rt}/save_transfer.cpp)
target_link_libraries(galaxy_save_transfer PUBLIC galaxy_runtime_abi galaxy_atomic_file nebula_bssl_crypto nebula_zinflate
    PRIVATE galaxy_mii_formats)
target_include_directories(galaxy_save_transfer PRIVATE "${galaxy_root}/native/petari/include")

# The host's extensions (host_extension.h, --extension).
add_library(galaxy_host_extension STATIC ${galaxy_rt}/host_extension.cpp ${galaxy_rt}/luma_services_host.cpp)
target_link_libraries(galaxy_host_extension PUBLIC galaxy_runtime_abi)
galaxy_runtime_lib_options(galaxy_host_extension)
add_library(galaxy_runtime_settings STATIC
    ${galaxy_rt}/runtime_settings.cpp
    ${galaxy_rt}/input_bindings.cpp
    ${galaxy_rt}/input/mod_actions.cpp
    ${galaxy_rt}/achievements.cpp
    ${galaxy_rt}/stats.cpp)
target_link_libraries(galaxy_runtime_settings PUBLIC galaxy_runtime_abi galaxy_atomic_file galaxy_host_extension)
galaxy_runtime_lib_options(galaxy_runtime_settings)

add_library(galaxy_host_pointer_events STATIC ${galaxy_rt}/host_pointer_events.cpp)
target_link_libraries(galaxy_host_pointer_events PUBLIC galaxy_runtime_abi)
galaxy_runtime_lib_options(galaxy_host_pointer_events)

add_library(galaxy_native_input STATIC
    ${galaxy_rt}/controller_roster.cpp
    ${galaxy_rt}/generic_hid.cpp
    ${galaxy_rt}/input/wiimote_input.cpp
    ${galaxy_rt}/input/wiimote_motion.cpp
    ${galaxy_rt}/input/wiimote_motion_pointer.cpp
    ${galaxy_rt}/native_input.cpp
    ${galaxy_rt}/playstation_controller.cpp
    ${galaxy_rt}/simple_pads.cpp
    ${galaxy_rt}/switch2_ble.cpp
    ${galaxy_rt}/pointer_control.cpp
    ${galaxy_rt}/rumble.cpp
    ${galaxy_rt}/steam_controller.cpp
    ${galaxy_rt}/switch_controller.cpp
    ${galaxy_rt}/tilt.cpp
    ${galaxy_rt}/wiimote_protocol.cpp
    ${galaxy_rt}/wiimote_speaker.cpp)
target_link_libraries(galaxy_native_input PUBLIC galaxy_runtime_abi)
galaxy_runtime_lib_options(galaxy_native_input)

# The game's sound mixed natively (audio_mixer.h).
add_library(galaxy_audio_mixer STATIC ${galaxy_rt}/audio_mixer.cpp ${galaxy_rt}/movie_audio.cpp)
target_include_directories(galaxy_audio_mixer PUBLIC "${galaxy_root}/runtime/include" "${galaxy_root}/native/include")
galaxy_runtime_lib_options(galaxy_audio_mixer)
