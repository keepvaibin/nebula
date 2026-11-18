/* Coverage builds only (CMakeLists.txt NEBULA_COVERAGE): the module's counts of what it ran are written when the host
   asks. The profile runtime writes them at the module's exit, which never runs: the host ends its process without
   running any library's detach (native/host/host_main.cpp finish_and_exit). Where they go: LLVM_PROFILE_FILE. */
int __llvm_profile_write_file(void);

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int nebula_game_write_counts(void) {
    return __llvm_profile_write_file();
}
