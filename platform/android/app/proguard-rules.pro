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
