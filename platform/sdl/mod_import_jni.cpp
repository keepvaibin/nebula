// SPDX-License-Identifier: GPL-3.0-only
// The launcher's mod import on Android (dev.nebula.galaxy.ModImport): galaxy::mods::import_mod, the importer the
// desktop runs, so a phone reads every archive and layout a PC does (.luma, zip, 7z, RAR 4 and 5, tar).
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"

#include <jni.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <string>

namespace {

// A Kotlin ByteArray of UTF-8 (a path as the file system spells it) as a string.
std::string text_of(JNIEnv* env, jbyteArray value) {
    if (value == nullptr) return {};
    std::string text(static_cast<std::size_t>(env->GetArrayLength(value)), '\0');
    env->GetByteArrayRegion(value, 0, static_cast<jsize>(text.size()), reinterpret_cast<jbyte*>(text.data()));
    return text;
}

}  // namespace

// Imports the file at `source` into <data>/mods (both UTF-8). `progress.update(done, total)` returning false cancels.
// The answer is UTF-8 "ok\nid\nkinds\ntitle\nmessage", ok being 1, 0, or 2 when the same mod was there already.
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_nebula_galaxy_ModImport_importFile(JNIEnv* env, jclass, jbyteArray source,
                                                                                   jbyteArray data, jobject progress) {
    namespace fs = std::filesystem;
    std::string answer;
    try {
        const fs::path folder = fs::path(text_of(env, data));
        galaxy::mods::set_mods_folder(folder / "mods", folder / "mods.ini", {});
        std::atomic<bool> cancel{false};
        jmethodID update = nullptr;
        if (progress != nullptr) {
            jclass type = env->GetObjectClass(progress);
            update = env->GetMethodID(type, "update", "(JJ)Z");
            // Not found (a build that renamed it): the import goes on without progress, no exception left pending.
            if (update == nullptr) env->ExceptionClear();
            env->DeleteLocalRef(type);
        }
        const galaxy::mods::ImportResult result = galaxy::mods::import_mod(
            fs::path(text_of(env, source)), folder / "mods",
            [&](const galaxy::mods::ImportProgress& state) {
                if (update == nullptr) return;
                const jboolean more = env->CallBooleanMethod(progress, update, static_cast<jlong>(state.done),
                                                             static_cast<jlong>(state.total));
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    return;
                }
                if (!more) cancel.store(true, std::memory_order_relaxed);
            },
            &cancel);
        answer = std::string(result.already ? "2" : result.ok ? "1" : "0") + "\n" + result.id + "\n" + result.kinds + "\n" + result.title + "\n" +
                 result.message;
    } catch (const std::exception& error) {
        // An exception must not cross into the Java VM.
        answer = std::string("0\n\n\n\nTHE IMPORT STOPPED: ") + error.what();
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(answer.size()));
    if (out != nullptr) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(answer.size()), reinterpret_cast<const jbyte*>(answer.data()));
    }
    return out;
}
