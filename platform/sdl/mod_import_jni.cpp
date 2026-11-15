// SPDX-License-Identifier: GPL-3.0-only
// The launcher's mod import on Android (dev.nebula.galaxy.ModImport): galaxy::mods::import_mod, the importer the
// desktop runs, so a phone reads every archive and layout a PC does (.luma, zip, 7z, RAR 4 and 5, tar), checked by the
// validator as the game checks an import (mod_check.h: the same call, against the mods placed, for the disc of game.pak).
#include "galaxy/asset_store.h"
#include "galaxy/luma.h"
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"

#include "../../runtime/src/mod_check.h"

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

// Imports the file at `source` into <data>/mods (all UTF-8), checked for the disc of `pak` (setup's game.pak; no disc
// when it is not one: the rules on the game's files are then not checked). `language` is the text table's column the
// launcher speaks (UsEnglish, EuGerman, JpJapanese...): the check's findings are in it. `progress.update(done, total)` returning
// false cancels. The answer is UTF-8 "ok\nid\nkinds\ntitle\nmessage", ok being 1, 0, or 2 when the same mod was there
// already; a refused mod's message has a line for each of the check's findings under it.
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_nebula_galaxy_ModImport_importFile(JNIEnv* env, jclass, jbyteArray source,
                                                                                   jbyteArray data, jbyteArray pak,
                                                                                   jbyteArray language, jobject progress) {
    namespace fs = std::filesystem;
    std::string answer;
    try {
        const fs::path folder = fs::path(text_of(env, data));
        const fs::path pak_path = fs::path(text_of(env, pak));
        const std::string game_id = galaxy::assets::store_game_id(pak_path);
        galaxy::mods::set_mods_folder(folder / "mods", folder / "mods.ini", game_id);
        // The game's own files for the rules on game paths, as the game's check reads them (not a store: unchecked).
        galaxy::assets::AssetStore store;
        const bool opened = !game_id.empty() && store.open(pak_path);
        std::string speaks = text_of(env, language);
        if (speaks.empty()) speaks = "UsEnglish";
        const galaxy::modlog::check::Game game{std::string(galaxy::luma::plugin_target()), game_id, speaks,
                                               opened ? &store : nullptr};
        const galaxy::mods::ImportCheck check = [&game](const fs::path& staged, const std::string& id,
                                                        galaxy::mods::ImportResult& result) {
            std::vector<galaxy::modlog::StartPackage> others;
            for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods()) {
                if (mod.package && mod.id != id) others.push_back({mod.id, mod.path, mod.enabled});
            }
            // No start's verdicts outside the game: a placed package is off only by its own report.
            const galaxy::modlog::ImportCheck found = galaxy::modlog::check::import(staged, others, game, {});
            result.checked = found.ran;
            result.refused = found.refused;
            for (const galaxy::modlog::ImportFinding& f : found.findings) {
                result.findings.push_back({f.error, f.code, f.file, f.text, f.line, f.column});
            }
            return !found.refused;
        };
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
            &cancel, check);
        std::string message = result.message;
        if (result.refused) {
            for (const galaxy::mods::ImportResult::Finding& f : result.findings) message += "\n" + f.text;
        }
        answer = std::string(result.already ? "2" : result.ok ? "1" : "0") + "\n" + result.id + "\n" + result.kinds + "\n" + result.title + "\n" +
                 message;
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
