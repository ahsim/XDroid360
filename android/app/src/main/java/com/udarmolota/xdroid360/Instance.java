package com.udarmolota.xdroid360;

import android.content.Context;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * One installed game: a game built on the phone from the player's disc (BuildService), an
 * older game package (docs/PACKAGE_FORMAT.md), or the development build's own game (the library
 * inside the APK, the game in the old dao/ folder). Its files: {@code root} (shared storage:
 * game/, userdata/, driver/, instance.json) and the game library in the app's internal files
 * (code can't be mapped from shared storage).
 */
final class Instance {
    static final String DEV_ID = "dev";
    static final String DAO_TITLE_ID = "454108C0";

    // Settings, kept in instance.json.
    /** auto: the built-in Turnip on an Adreno 8xx (what it is made for), else the phone's own. */
    static final String DRIVER_AUTO = "auto", DRIVER_BUILTIN = "builtin", DRIVER_SYSTEM = "system", DRIVER_CUSTOM = "custom";
    static final String SYSMEM_AUTO = "auto", SYSMEM_ON = "on", SYSMEM_OFF = "off";

    final String id;
    final File root;
    /** The game library to load, or null for the one inside the APK. */
    final File library;
    String name;
    JSONObject manifest; // the package's manifest.json (empty for the development build)
    String driver = DRIVER_AUTO;
    String sysmem = SYSMEM_AUTO;
    // New instances start at the lowest texture detail: a phone's memory and bandwidth, not the
    // console's (the choice stays in the settings).
    int textureLodBias = 2;
    /** The in-game display: nothing, the FPS counter, or the performance bar (as in Zomdroid). */
    static final int HUD_OFF = 0, HUD_FPS = 1, HUD_FULL = 2;
    int hudMode = HUD_OFF;
    /** The game's language (config.toml [game] language), "" for the phone's. */
    String language = "";
    /** The GPU's measurements in the game's log, for a bug report (DAO_GPU_STATS, DAO_GPU_TIMING). */
    boolean diagnostics = false;
    /** The languages the supported discs have text for (both games read the same key). */
    static final String[] LANGUAGES = { "english", "italian", "spanish" };
    boolean vibrateOnTouch = false;
    /** The frame rate picked, 0 for the package's default (options.frame_rate). */
    int frameRate = 0;
    /** The graphics preset picked (options.preset), "" for the game's own settings. */
    String preset = "";
    /** Whether the launcher wrote the preset's target file (so going back to "" removes it). */
    boolean presetWritten = false;
    /** The renderer (config.toml [xenia] dao_gpu_renderer): Xenia's emulation, or the native one. */
    static final String RENDERER_XENIA = "xenia", RENDERER_NATIVE = "native";
    /** The player's choice, "" for the game's default (rendererToUse). */
    String renderer = "";

    /** The renderer to start with: the chosen one, else native for Dragon Age: Origins, else Xenia. */
    String rendererToUse() {
        if (RENDERER_NATIVE.equals(renderer) || RENDERER_XENIA.equals(renderer)) return renderer;
        return DAO_TITLE_ID.equals(titleId()) ? RENDERER_NATIVE : RENDERER_XENIA;
    }

    private Instance(String id, File root, File library) {
        this.id = id;
        this.root = root;
        this.library = library;
    }

    File game() { return new File(root, "game"); }
    File userdata() { return new File(root, "userdata"); }
    File driverDir() { return new File(root, "driver"); }
    File presetsDir() { return new File(root, "presets"); }
    boolean isDev() { return DEV_ID.equals(id); }

    /** The first .so in driver/ (a custom driver), or null. */
    File customDriver() {
        File[] files = driverDir().listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.isFile() && f.getName().endsWith(".so")) return f;
            }
        }
        return null;
    }

    /**
     * The game's title ID (upper case): which game this is, for its saves folder and for sharing a
     * shader cache only between instances of the same game. State of Decay's format 1 packages
     * carry DAO's pins and their own ID in build.sod_title_id; that case goes once SoD writes
     * format 2.
     */
    String titleId() {
        JSONObject build = manifest.optJSONObject("build");
        String sod = build != null ? build.optString("sod_title_id") : "";
        if (!sod.isEmpty()) return sod.toUpperCase(java.util.Locale.ROOT);
        return manifest.optString("title_id", DAO_TITLE_ID).toUpperCase(java.util.Locale.ROOT);
    }

    /** An optional setting the package offers (manifest "options", format 2), or null. */
    JSONObject option(String name) {
        JSONObject options = manifest.optJSONObject("options");
        return options != null ? options.optJSONObject(name) : null;
    }

    /** The frame rate to run at, or 0 when the package offers no choice. */
    int frameRateToUse() {
        JSONObject o = option("frame_rate");
        if (o == null) return 0;
        org.json.JSONArray choices = o.optJSONArray("choices");
        if (choices == null || choices.length() == 0) return 0;
        for (int i = 0; i < choices.length(); i++) if (choices.optInt(i) == frameRate) return frameRate;
        return o.optInt("default", choices.optInt(0));
    }

    /** The package's preset ids that came with it (presets/<id>.cfg), in the manifest's order. */
    List<String> presets() {
        List<String> out = new ArrayList<>();
        JSONObject o = option("preset");
        org.json.JSONArray choices = o != null ? o.optJSONArray("choices") : null;
        if (choices == null) return out;
        for (int i = 0; i < choices.length(); i++) {
            String id = choices.optString(i);
            if (id.matches("[a-z0-9_]+") && new File(presetsDir(), id + ".cfg").isFile()) out.add(id);
        }
        return out;
    }

    String buildLabel() {
        if (isDev()) return "development build (game built into the APK)";
        JSONObject recipe = manifest.optJSONObject("recipe");
        if (recipe != null) return "built on this phone, profile " + recipe.optString("version", "?");
        JSONObject build = manifest.optJSONObject("build");
        String commit = build != null ? build.optString("daodroid", "?") : "?";
        if (commit.length() > 8) commit = commit.substring(0, 8);
        Object tu = manifest.opt("title_update");
        return "build " + commit + (tu instanceof Number ? ", Title Update " + tu : "");
    }

    void save() throws IOException {
        JSONObject o = new JSONObject();
        try {
            o.put("name", name);
            o.put("manifest", manifest);
            JSONObject s = new JSONObject();
            s.put("driver", driver);
            s.put("sysmem", sysmem);
            s.put("texture_lod_bias", textureLodBias);
            s.put("hud_mode", hudMode);
            s.put("language", language);
            s.put("diagnostics", diagnostics);
            s.put("vibrate_on_touch", vibrateOnTouch);
            s.put("frame_rate", frameRate);
            s.put("preset", preset);
            s.put("preset_written", presetWritten);
            s.put("renderer", renderer);
            o.put("settings", s);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        //noinspection ResultOfMethodCallIgnored
        root.mkdirs();
        String text;
        try {
            text = o.toString(2);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        Files.write(new File(root, "instance.json").toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private void load() {
        File f = new File(root, "instance.json");
        if (!f.isFile()) return;
        try {
            JSONObject o = new JSONObject(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8));
            name = o.optString("name", name);
            JSONObject m = o.optJSONObject("manifest");
            if (m != null) manifest = m;
            JSONObject s = o.optJSONObject("settings");
            if (s != null) {
                driver = s.optString("driver", driver);
                sysmem = s.optString("sysmem", sysmem);
                textureLodBias = s.optInt("texture_lod_bias", textureLodBias);
                // Before hud_mode: the performance bar was a switch, on unless turned off.
                language = s.optString("language", language);
                diagnostics = s.optBoolean("diagnostics", diagnostics);
                hudMode = s.has("hud_mode") ? s.optInt("hud_mode", hudMode)
                        : s.optBoolean("perf_overlay", true) ? HUD_FULL : HUD_OFF;
                vibrateOnTouch = s.optBoolean("vibrate_on_touch", vibrateOnTouch);
                frameRate = s.optInt("frame_rate", frameRate);
                preset = s.optString("preset", preset);
                presetWritten = s.optBoolean("preset_written", presetWritten);
                renderer = s.optString("renderer", renderer);
            }
        } catch (IOException | JSONException e) {
            android.util.Log.w("XDroid360", "instance.json of " + id + ": " + e);
        }
    }

    // ---------------------------------------------------------------- the store

    /** dao/ on the shared storage: the development build's game, and instances/. */
    static File daoRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        return new File(external != null ? external : context.getFilesDir(), "dao");
    }

    static File instancesRoot(Context context) {
        return new File(daoRoot(context), "instances");
    }

    static File libraryFor(Context context, String id) {
        return new File(new File(new File(context.getFilesDir(), "instances"), id), "libgame.so");
    }

    /** Whether this APK holds the game library (the development build). */
    static boolean apkHasGameLibrary(Context context) {
        return new File(context.getApplicationInfo().nativeLibraryDir, "libgame.so").isFile();
    }

    static Instance open(Context context, String id) {
        Instance i;
        if (DEV_ID.equals(id)) {
            i = new Instance(id, daoRoot(context), null);
            i.name = "Dragon Age: Origins (development)";
            i.manifest = new JSONObject();
            i.driver = DRIVER_CUSTOM; // the dev build's driver/ folder, as before the launcher
        } else {
            i = new Instance(id, new File(instancesRoot(context), id), libraryFor(context, id));
            i.name = "Dragon Age: Origins";
            i.manifest = new JSONObject();
        }
        i.load();
        return i;
    }

    static List<Instance> list(Context context) {
        List<Instance> out = new ArrayList<>();
        if (apkHasGameLibrary(context) && new File(new File(daoRoot(context), "game"), "default.xex").isFile()) {
            out.add(open(context, DEV_ID));
        }
        File[] dirs = instancesRoot(context).listFiles();
        if (dirs != null) {
            java.util.Arrays.sort(dirs);
            for (File d : dirs) {
                // An instance is complete once its instance.json and library are in place.
                if (d.isDirectory() && new File(d, "instance.json").isFile()
                        && libraryFor(context, d.getName()).isFile()) {
                    out.add(open(context, d.getName()));
                }
            }
        }
        return out;
    }

    /** The instance a build on the phone made (BuildJob id), with the recipe's stamp as its manifest. */
    static Instance built(Context context, String id, String titleId, String title, JSONObject stamp) {
        Instance i = new Instance(id, new File(instancesRoot(context), id), libraryFor(context, id));
        i.manifest = new JSONObject();
        try {
            i.manifest.put("format", 2);
            i.manifest.put("title_id", titleId);
            i.manifest.put("title", title);
            i.manifest.put("recipe", stamp);
        } catch (JSONException e) {
            throw new IllegalStateException(e);
        }
        i.name = title;
        return i;
    }

    /** The language to start the game in: the chosen one, else the phone's (Italian, Spanish), else English. */
    String languageToUse() {
        if (!language.isEmpty()) return language;
        switch (java.util.Locale.getDefault().getLanguage()) {
            case "it": return "italian";
            case "es": return "spanish";
            default: return "english";
        }
    }

    /** name, or "name 2", "name 3"... if another instance already has it. */
    static String uniqueName(java.util.List<Instance> all, String name) {
        java.util.Set<String> taken = new java.util.HashSet<>();
        for (Instance i : all) taken.add(i.name);
        String unique = name;
        for (int n = 2; taken.contains(unique); n++) unique = name + " " + n;
        return unique;
    }

    /** Removes the instance's files (its game, saves and library). */
    void delete(Context context) {
        if (isDev()) {
            // The development build's files share dao/ with the instances: only its own go.
            for (String name : new String[] { "game", "userdata", "driver", "title_update", "instance.json", "banner.jpg" }) {
                deleteTree(new File(root, name));
            }
            return;
        }
        deleteTree(root);
        if (library != null) deleteTree(library.getParentFile());
    }

    static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) deleteTree(c);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
