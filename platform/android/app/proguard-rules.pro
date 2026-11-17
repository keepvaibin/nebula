# SDL's Java half is called from libSDL3.so by name.
-keep class org.libsdl.app.** { *; }

# Called from libmain.so and libnebula_import.so by name (platform/sdl/host_window_sdl.cpp call_activity). A method
# with parameters is named here with them: R8 removes one it does not see called from Java, and the native call then
# finds nothing (build_apk.ps1 checks every name the native code calls against the built APK).
-keep class dev.nebula.galaxy.GameActivity {
    native <methods>;
    public void *();
    public void pickFiles(int);
    public void restartGame(int);
}
# The real Wii Remote's USB link (platform/sdl/wiimote_link_android.cpp) calls these by name.
-keep class dev.nebula.galaxy.WiimoteUsb {
    native <methods>;
    static int[] slots();
    static boolean open(int);
    static boolean write(int, byte[], int);
    static void close(int);
}
-keep class dev.nebula.galaxy.Importer {
    native <methods>;
}
# LumaCore's clipboard pictures (platform/sdl/luma_system_sdl.cpp) calls these by name.
-keep class dev.nebula.galaxy.ClipboardBridge {
    static boolean hasImage(android.app.Activity);
    static byte[] readImage(android.app.Activity, int);
    static boolean writeImage(android.app.Activity, byte[]);
}
-keep interface dev.nebula.galaxy.Importer$Progress { *; }
-keep class * implements dev.nebula.galaxy.Importer$Progress {
    public boolean update(long, long);
}

# Called from libnebula_mods.so by name (platform/sdl/mod_import_jni.cpp, mii_import_jni.cpp).
-keep class dev.nebula.galaxy.ModImport {
    native <methods>;
}
-keep class dev.nebula.galaxy.MiiImport {
    native <methods>;
}
-keep interface dev.nebula.galaxy.ModImport$Progress { *; }
-keep class * implements dev.nebula.galaxy.ModImport$Progress {
    public boolean update(long, long);
}
