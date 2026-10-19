// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

/** The player's disc image to game.pak, in libnebula_import.so (crates/nebula-import). */
final class Importer {
    static {
        System.loadLibrary("nebula_import");
    }

    interface Progress {
        /** Bytes written of the total; false cancels. */
        boolean update(long done, long total);
    }

    private Importer() {}

    /** {"supported", "gameId", "title", "format", "message"} for the image open on {@code fd}. */
    static native String identify(int fd);

    /** Writes game.pak at {@code output} (through a temporary file beside it); null when done, else the reason. */
    static native String build(int fd, String output, Progress progress);
}
