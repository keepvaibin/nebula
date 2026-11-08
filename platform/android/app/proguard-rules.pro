# SDL's Java half is called from libSDL3.so by name.
-keep class org.libsdl.app.** { *; }

# Called from libmain.so and libnebula_import.so by name.
-keep class dev.nebula.galaxy.GameActivity {
    native <methods>;
    public void *();
}
-keep class dev.nebula.galaxy.Importer {
    native <methods>;
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
