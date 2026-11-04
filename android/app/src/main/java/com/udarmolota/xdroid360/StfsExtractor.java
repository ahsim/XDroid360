package com.udarmolota.xdroid360;

/**
 * State of Decay's STFS packages, read in-process by SoD's libstfsextract.so (its JNI exports
 * bind to this class; SD1/toolchain/RECIPE_FORMAT_DRAFT.md "STFS extraction"). The fd is the
 * player's file opened through SAF, so no child process needs it.
 */
final class StfsExtractor {
    /** Error codes of nativeExtract, in the order of the recipe's failure codes they map to. */
    static final String[] ERRORS = { null, "source_unreadable", "source_not_seekable", "extract_failed",
                                     "not_enough_space", "cancelled" };

    private static Boolean loaded;

    private StfsExtractor() {}

    interface Progress {
        /** false cancels. */
        boolean onProgress(long done, long total);
    }

    /** Whether this APK carries the extractor (a launcher without a profile that needs it may not). */
    static synchronized boolean available() {
        if (loaded == null) {
            try {
                System.loadLibrary("stfsextract");
                loaded = true;
            } catch (UnsatisfiedLinkError e) {
                android.util.Log.w("XDroid360", "no STFS extractor: " + e);
                loaded = false;
            }
        }
        return loaded;
    }

    /**
     * The package's header as JSON ({"title_id":"584111E8","content_type":"0x000D0000",
     * "display_name":...,"size":...}), or null when fd isn't an STFS package.
     */
    static native String nativeProbe(int fd);

    /** Extracts the package under outDir (game/, dlc/<name>/, tu/ by content type); 0 or ERRORS' index. */
    static native int nativeExtract(int fd, String outDir, Progress progress);
}
