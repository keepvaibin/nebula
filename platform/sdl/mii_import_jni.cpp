// SPDX-License-Identifier: GPL-3.0-only
// The launcher's Mii import on Android (dev.nebula.galaxy.MiiImport): the game's own Mii reader (mii_formats.h), so a
// phone takes every console's Mii files a PC does. The files go to the folder each start imports (mii_import.h); the
// game's own checks run there, as the launcher's process has no game.
#include "galaxy/mii_import.h"

#include <jni.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <span>
#include <string>

namespace {

// A Kotlin ByteArray of UTF-8 (a path or name as the file system spells it) as a string.
std::string text_of(JNIEnv* env, jbyteArray value) {
    if (value == nullptr) return {};
    std::string text(static_cast<std::size_t>(env->GetArrayLength(value)), '\0');
    env->GetByteArrayRegion(value, 0, static_cast<jsize>(text.size()), reinterpret_cast<jbyte*>(text.data()));
    return text;
}

jbyteArray bytes_of(JNIEnv* env, const std::string& text) {
    jbyteArray out = env->NewByteArray(static_cast<jsize>(text.size()));
    if (out != nullptr) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    }
    return out;
}

}  // namespace

// The Miis in `content` (a file named `name`), kept in the folder `folder` (both UTF-8). The answer is UTF-8
// "miis\nconverted\nkept\nerror", kept being 1 or 0.
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_nebula_galaxy_MiiImport_addFile(JNIEnv* env, jclass, jbyteArray folder,
                                                                                jbyteArray name, jbyteArray content) {
    std::string answer;
    try {
        std::string bytes = text_of(env, content);
        const galaxy::host::MiiFolderAdd added = galaxy::host::add_to_mii_folder(
            std::filesystem::path(text_of(env, folder)), std::filesystem::path(text_of(env, name)),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        answer = std::to_string(added.miis) + "\n" + std::to_string(added.converted) + "\n" + (added.kept ? "1" : "0") +
                 "\n" + added.error;
    } catch (const std::exception& error) {
        // An exception must not cross into the Java VM.
        answer = std::string("0\n0\n0\n") + error.what();
    }
    return bytes_of(env, answer);
}

// The Miis in the library file at `file` (UTF-8); -1 when there is none or it is damaged.
extern "C" JNIEXPORT jint JNICALL Java_dev_nebula_galaxy_MiiImport_libraryCount(JNIEnv* env, jclass, jbyteArray file) {
    try {
        const auto count = galaxy::host::mii_library_file_count(std::filesystem::path(text_of(env, file)));
        return count ? static_cast<jint>(*count) : -1;
    } catch (const std::exception&) {
        return -1;
    }
}
