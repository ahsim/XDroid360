package com.udarmolota.xdroid360;

import android.content.Context;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Imports a game package (.x360pkg, or an older .daopkg; docs/PACKAGE_FORMAT.md) into a new instance: reads
 * manifest.json (the first entry), checks it, then streams the entries into place. A package is
 * a stored ZIP, so this is a plain copy (ZipInputStream reads the local headers in order; the
 * entries are each under 4 GB, so no ZIP64 local headers).
 */
final class PackageImporter {
    /** The newest format this launcher reads; format 1 (.daopkg) is the same layout without "title". */
    static final int FORMAT = 2;
    static final String LIBRARY_ENTRY = "lib/arm64-v8a/libgame.so";

    interface Progress {
        void onProgress(long done, long total, String entry);
        boolean cancelled();
    }

    static final class Problem extends Exception {
        Problem(String message) { super(message); }
    }

    /** Reads and checks manifest.json. */
    static JSONObject readManifest(Context context, Uri uri) throws IOException, Problem {
        try (InputStream in = context.getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(new BufferedInputStream(in, 1 << 16))) {
            ZipEntry first = zip.getNextEntry();
            if (first == null || !"manifest.json".equals(first.getName())) {
                throw new Problem("This isn't a game package (no manifest.json first).");
            }
            String text = new String(readAll(zip), StandardCharsets.UTF_8);
            JSONObject m;
            try {
                m = new JSONObject(text);
            } catch (JSONException e) {
                throw new Problem("The package's manifest.json is damaged.");
            }
            int format = m.optInt("format", -1);
            if (format > FORMAT) {
                throw new Problem("This package is newer than this launcher (format " + format + "): update XDroid360.");
            }
            if (format < 1) {
                throw new Problem("Unknown package format " + format + ".");
            }
            int abi = m.optInt("abi", -1);
            if (abi != GameActivity.DAO_ABI) {
                throw new Problem(abi > GameActivity.DAO_ABI
                        ? "This package was built for a newer XDroid360 (version " + abi + "): update the launcher."
                        : "This package was built for an older XDroid360 (version " + abi + "): build it again with the current tools.");
            }
            // Which edition of the game the code matches is checked by the PC tool when it builds the
            // package and by the game library when it starts; the launcher only needs to know the game.
            if (!m.optString("title_id").matches("[0-9A-Fa-f]{8}")) {
                throw new Problem("The package's manifest.json has no valid title ID.");
            }
            return m;
        }
    }

    /** Copies the package into the instance; on failure or cancel, the instance is removed. */
    static void importInto(Context context, Uri uri, Instance instance, Progress progress) throws IOException, Problem {
        long total = instance.manifest.optLong("game_bytes", 0);
        long done = 0;
        boolean ok = false;
        try (InputStream in = context.getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(new BufferedInputStream(in, 1 << 20))) {
            byte[] buffer = new byte[1 << 20];
            boolean haveLibrary = false;
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                if (progress.cancelled()) throw new Problem("Cancelled.");
                String name = e.getName();
                if (e.isDirectory() || "manifest.json".equals(name)) continue;
                if (name.contains("..")) throw new Problem("Bad entry in the package: " + name);
                File target;
                MessageDigest sha = null;
                if (LIBRARY_ENTRY.equals(name)) {
                    target = instance.library;
                    sha = sha256();
                    haveLibrary = true;
                } else if (name.startsWith("game/") || name.startsWith("title_update/") || name.startsWith("presets/")) {
                    target = new File(instance.root, name);
                } else if ("banner.png".equals(name) || "banner.jpg".equals(name)) {
                    target = Banner.file(instance); // the package's own picture over the splash screen
                } else {
                    continue; // not part of formats 1 and 2
                }
                //noinspection ResultOfMethodCallIgnored
                target.getParentFile().mkdirs();
                try (OutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                        if (sha != null) sha.update(buffer, 0, n);
                        if (name.startsWith("game/")) {
                            done += n;
                            progress.onProgress(done, total, name);
                        }
                        if (progress.cancelled()) throw new Problem("Cancelled.");
                    }
                }
                if (sha != null && !hex(sha.digest()).equals(instance.manifest.optString("library_sha256"))) {
                    throw new Problem("The game library in the package is damaged (checksum mismatch).");
                }
            }
            if (!haveLibrary) throw new Problem("The package has no game library.");
            if (!new File(instance.game(), "default.xex").isFile()) throw new Problem("The package has no game files.");
            //noinspection ResultOfMethodCallIgnored
            instance.userdata().mkdirs();
            Instance warm = InstanceActions.warmestOther(Instance.list(context), instance);
            if (warm != null) {
                try {
                    InstanceActions.copyShaderCache(warm, instance);
                } catch (IOException cacheError) {
                    android.util.Log.w("XDroid360", "shader cache from " + warm.id + ": " + cacheError);
                }
            }
            instance.name = Instance.uniqueName(Instance.list(context), instance.name);
            instance.save(); // last: an instance without instance.json isn't listed
            ok = true;
        } finally {
            if (!ok) instance.delete(context);
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int n;
        while ((n = in.read(b)) > 0) out.write(b, 0, n);
        return out.toByteArray();
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder();
        for (byte b : bytes) s.append(String.format("%02x", b));
        return s.toString();
    }
}
