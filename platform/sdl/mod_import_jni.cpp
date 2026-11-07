// SPDX-License-Identifier: GPL-3.0-only
// The launcher's mod import on Android (dev.nebula.galaxy.ModImport): galaxy::mods::import_mod, the importer the
// desktop runs, so a phone reads every archive and layout a PC does (.luma, zip, 7z, RAR 4 and 5, tar).
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"

#include <jni.h>

#include <atomic>
#include <filesystem>
#include <string>

namespace {

std::string text_of(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string text = chars != nullptr ? chars : "";
    if (chars != nullptr) env->ReleaseStringUTFChars(value, chars);
    return text;
}

}  // namespace

// Imports the file at `source` into <data>/mods. `progress.update(done, total)` returning false cancels. The answer is
// "ok\nid\nkinds\nmessage", ok being 1 or 0.
extern "C" JNIEXPORT jstring JNICALL Java_dev_nebula_galaxy_ModImport_importFile(JNIEnv* env, jclass, jstring source,
                                                                                jstring data, jobject progress) {
    namespace fs = std::filesystem;
    const fs::path folder = fs::path(text_of(env, data));
    galaxy::mods::set_mods_folder(folder / "mods", folder / "mods.ini", {});
    std::atomic<bool> cancel{false};
    jmethodID update = nullptr;
    if (progress != nullptr) {
        jclass type = env->GetObjectClass(progress);
        update = env->GetMethodID(type, "update", "(JJ)Z");
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
    const std::string answer =
        std::string(result.ok ? "1" : "0") + "\n" + result.id + "\n" + result.kinds + "\n" + result.message;
    return env->NewStringUTF(answer.c_str());
}
