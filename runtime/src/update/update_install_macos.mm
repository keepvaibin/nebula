// SPDX-License-Identifier: GPL-3.0-only
// The update's install step on macOS: the running Nebula.app swapped with the verified release's. The package (the .app
// zipped by `ditto -c -k --keepParent`) is extracted with /usr/bin/ditto into a staging folder beside the bundle (the
// same volume, so the swap is one rename), the new bundle's code signature is checked against the running bundle's
// designated requirement (same team, same identifier), the two are exchanged with renamex_np(RENAME_SWAP), and the old
// bundle is kept as "Nebula.previous.app" for rollback. An unsigned or ad-hoc signed build, a bundle the user cannot
// write, or one run from a translocated (quarantined, read-only) path installs nothing: can_install() is false there.
// Requires -fobjc-arc (as the other Apple host sources).
#include "update_internal.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cerrno>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace galaxy::update::detail {

namespace {

std::filesystem::path running_bundle() {
    @autoreleasepool {
        NSString* path = [[NSBundle mainBundle] bundlePath];
        if (path == nil || ![path hasSuffix:@".app"]) return {};
        return std::filesystem::path(path.fileSystemRepresentation);
    }
}

// The running code's designated requirement, null when it has none worth checking against (ad-hoc or unsigned).
SecRequirementRef running_requirement() {
    SecCodeRef self = nullptr;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &self) != errSecSuccess) return nullptr;
    SecRequirementRef requirement = nullptr;
    CFDictionaryRef information = nullptr;
    SecStaticCodeRef staticSelf = nullptr;
    bool team = false;
    if (SecCodeCopyStaticCode(self, kSecCSDefaultFlags, &staticSelf) == errSecSuccess &&
        SecCodeCopySigningInformation(staticSelf, kSecCSSigningInformation, &information) == errSecSuccess && information != nullptr) {
        team = CFDictionaryGetValue(information, kSecCodeInfoTeamIdentifier) != nullptr;
    }
    if (team) (void)SecCodeCopyDesignatedRequirement(staticSelf, kSecCSDefaultFlags, &requirement);
    if (information != nullptr) CFRelease(information);
    if (staticSelf != nullptr) CFRelease(staticSelf);
    CFRelease(self);
    return requirement;
}

bool run(const char* tool, const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(tool));
    for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child = 0;
    if (posix_spawn(&child, tool, nullptr, nullptr, argv.data(), environ) != 0) return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

std::string platform_asset(bool beta) {
#if TARGET_OS_OSX
    return beta ? "Nebula-Beta-macOS.zip" : "Nebula-macOS.zip";
#else
    (void)beta;
    return {};  // iPhone and iPad: the App Store
#endif
}

bool platform_can_install() noexcept {
#if TARGET_OS_OSX
    const std::filesystem::path bundle = running_bundle();
    if (bundle.empty() || bundle.string().find("/AppTranslocation/") != std::string::npos) return false;
    if (access(bundle.parent_path().c_str(), W_OK) != 0) return false;
    SecRequirementRef requirement = running_requirement();
    if (requirement == nullptr) return false;
    CFRelease(requirement);
    return true;
#else
    return false;
#endif
}

bool platform_install(const std::filesystem::path& package, std::string& error) {
    const std::filesystem::path bundle = running_bundle();
    if (bundle.empty()) {
        error = "not running from an app bundle";
        return false;
    }
    const std::filesystem::path staging = bundle.parent_path() / ".nebula-update";
    std::error_code failure;
    std::filesystem::remove_all(staging, failure);
    std::filesystem::create_directories(staging, failure);
    if (!run("/usr/bin/ditto", {"-x", "-k", package.string(), staging.string()})) {
        std::filesystem::remove_all(staging, failure);
        error = "could not extract the package";
        return false;
    }
    std::filesystem::path fresh;
    for (const auto& entry : std::filesystem::directory_iterator(staging, failure)) {
        if (entry.path().extension() == ".app") fresh = entry.path();
    }
    SecRequirementRef requirement = running_requirement();
    bool valid = false;
    if (!fresh.empty() && requirement != nullptr) {
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:fresh.c_str()] isDirectory:YES];
            SecStaticCodeRef code = nullptr;
            if (SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &code) == errSecSuccess) {
                valid = SecStaticCodeCheckValidity(code, kSecCSCheckAllArchitectures | kSecCSStrictValidate | kSecCSCheckNestedCode,
                                                   requirement) == errSecSuccess;
                CFRelease(code);
            }
        }
    }
    if (requirement != nullptr) CFRelease(requirement);
    if (!valid) {
        std::filesystem::remove_all(staging, failure);
        error = "the new bundle's signature does not match this Nebula's";
        return false;
    }
    if (renamex_np(fresh.c_str(), bundle.c_str(), RENAME_SWAP) != 0) {
        std::filesystem::remove_all(staging, failure);
        error = "could not swap the app bundle";
        return false;
    }
    // `fresh` now holds the version that was running: kept for rollback.
    const std::filesystem::path previous = bundle.parent_path() / (bundle.stem().string() + ".previous.app");
    std::filesystem::remove_all(previous, failure);
    std::filesystem::rename(fresh, previous, failure);
    std::filesystem::remove_all(staging, failure);
    std::filesystem::remove(package, failure);
    return true;
}

}  // namespace galaxy::update::detail
