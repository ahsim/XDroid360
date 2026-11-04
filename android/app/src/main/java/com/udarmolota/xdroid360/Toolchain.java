package com.udarmolota.xdroid360;

import android.content.Context;
import android.content.pm.PackageManager;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * What the APK carries for building games on the phone: the tools (clang, lld, XenonRecomp as
 * lib*.so in nativeLibraryDir), the game-agnostic kit (assets/kit.zip: sysroot, libc++, clang's
 * headers) and one profile per game (assets/games/<id>.zip: recipe.json, the prebuilt runtime).
 * The zips are unpacked into internal storage once per launcher version.
 */
final class Toolchain {
    private static final String KIT_ASSET = "kit.zip";
    private static final String GAMES = "games";
    private static List<Recipe> profiles;

    private Toolchain() {}

    static File root(Context context) { return new File(context.getFilesDir(), "toolchain"); }
    static File kitDir(Context context) { return new File(root(context), "kit"); }
    static File profileDir(Context context, String id) { return new File(new File(root(context), GAMES), id); }
    static File tools(Context context) { return new File(context.getApplicationInfo().nativeLibraryDir); }

    /** Whether this APK can build games at all (a dev build may lack the toolchain). */
    static boolean present(Context context) {
        return new File(tools(context), "libclang.so").isFile() && assetExists(context, KIT_ASSET);
    }

    /** The profiles in the APK, by their recipes (read from the zips, not unpacked). */
    static synchronized List<Recipe> profiles(Context context) {
        if (profiles != null) return profiles;
        List<Recipe> out = new ArrayList<>();
        try {
            String[] names = context.getAssets().list(GAMES);
            if (names != null) {
                for (String name : names) {
                    if (!name.endsWith(".zip")) continue;
                    String id = name.substring(0, name.length() - 4);
                    byte[] json = entry(context, GAMES + "/" + name, "recipe.json");
                    if (json == null) continue;
                    try {
                        out.add(new Recipe(id, new JSONObject(new String(json, StandardCharsets.UTF_8))));
                    } catch (JSONException | Recipe.Failure e) {
                        android.util.Log.w("XDroid360", "profile " + id + ": " + e);
                    }
                }
            }
        } catch (IOException e) {
            android.util.Log.w("XDroid360", "profiles: " + e);
        }
        profiles = out;
        return out;
    }

    static Recipe profile(Context context, String id) {
        for (Recipe r : profiles(context)) if (r.profileId.equals(id)) return r;
        return null;
    }

    /** The kit's kit.json ({"kit": 1, "ndk": ..., "tools": {...}}), or an empty object. */
    static JSONObject kitJson(Context context) {
        try {
            byte[] b = entry(context, KIT_ASSET, "kit.json");
            if (b != null) return new JSONObject(new String(b, StandardCharsets.UTF_8));
        } catch (IOException | JSONException e) {
            android.util.Log.w("XDroid360", "kit.json: " + e);
        }
        return new JSONObject();
    }

    /** Why this APK's kit can't build recipe (the kit or tool versions it requires), or null. */
    static String mismatch(Context context, Recipe recipe) {
        JSONObject kit = kitJson(context);
        if (kit.optInt("kit", -1) != recipe.kit()) return "kit " + kit.optInt("kit", -1) + ", recipe needs " + recipe.kit();
        if (recipe.abi() != GameActivity.DAO_ABI) return "ABI " + GameActivity.DAO_ABI + ", recipe needs " + recipe.abi();
        JSONObject have = kit.optJSONObject("tools");
        JSONObject need = recipe.tools();
        for (Iterator<String> it = need.keys(); it.hasNext(); ) {
            String tool = it.next();
            String v = have != null ? have.optString(tool) : "";
            if (!v.equals(need.optString(tool))) return tool + " " + v + ", recipe needs " + need.optString(tool);
        }
        return null;
    }

    /** Unpacks the kit and recipe's profile (once per launcher version). */
    static void unpack(Context context, Recipe recipe) throws IOException {
        unpackOnce(context, KIT_ASSET, kitDir(context));
        unpackOnce(context, GAMES + "/" + recipe.profileId + ".zip", profileDir(context, recipe.profileId));
    }

    private static void unpackOnce(Context context, String asset, File dir) throws IOException {
        String version = launcherVersion(context);
        File stamp = new File(dir, ".unpacked");
        if (stamp.isFile() && version.equals(new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8))) {
            return;
        }
        Instance.deleteTree(dir);
        String root = dir.getCanonicalPath();
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = context.getAssets().open(asset);
             ZipInputStream zip = new ZipInputStream(new BufferedInputStream(in, 1 << 16))) {
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                File target = new File(dir, e.getName());
                if (!target.getCanonicalPath().startsWith(root + File.separator)) {
                    throw new IOException(asset + ": bad entry " + e.getName());
                }
                if (e.isDirectory()) {
                    //noinspection ResultOfMethodCallIgnored
                    target.mkdirs();
                    continue;
                }
                //noinspection ResultOfMethodCallIgnored
                target.getParentFile().mkdirs();
                try (OutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) out.write(buffer, 0, n);
                }
            }
        }
        Files.write(stamp.toPath(), version.getBytes(StandardCharsets.UTF_8));
    }

    private static String launcherVersion(Context context) {
        try {
            return String.valueOf(context.getPackageManager().getPackageInfo(context.getPackageName(), 0).lastUpdateTime);
        } catch (PackageManager.NameNotFoundException e) {
            return "?";
        }
    }

    private static boolean assetExists(Context context, String asset) {
        try (InputStream ignored = context.getAssets().open(asset)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /** One entry of a zip asset, or null. */
    private static byte[] entry(Context context, String asset, String name) throws IOException {
        try (InputStream in = context.getAssets().open(asset);
             ZipInputStream zip = new ZipInputStream(new BufferedInputStream(in, 1 << 16))) {
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                if (!name.equals(e.getName())) continue;
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] b = new byte[8192];
                int n;
                while ((n = zip.read(b)) > 0) out.write(b, 0, n);
                return out.toByteArray();
            }
        } catch (java.io.FileNotFoundException e) {
            return null;
        }
        return null;
    }
}
