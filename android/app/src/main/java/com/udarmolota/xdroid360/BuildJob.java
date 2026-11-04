package com.udarmolota.xdroid360;

import android.content.Context;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * A game being built on the phone, kept in build.json next to its library (internal storage),
 * so a build Android killed shows up again as interrupted and can continue. The instance gets
 * its instance.json only when the build is done, so until then it isn't listed as playable.
 */
final class BuildJob {
    static final String RUNNING = "running", FAILED = "failed", CANCELLED = "cancelled", DONE = "done";

    final String id;
    String profileId;
    Uri source;
    String pick;
    String name;
    String titleId = "";
    String state = RUNNING;
    String errorCode = "";
    String errorDetail = "";
    String failedStep = "";
    JSONObject stamp = new JSONObject(); // the recipe's {format, game, version, kit, tools} when it started

    private BuildJob(String id) {
        this.id = id;
    }

    static BuildJob create(String profileId, Uri source, String pick, String name) {
        BuildJob j = new BuildJob("i" + System.currentTimeMillis());
        j.profileId = profileId;
        j.source = source;
        j.pick = pick;
        j.name = name;
        return j;
    }

    /** The instance's internal folder: libgame.so, build.json and the build's scratch folder. */
    static File internal(Context context, String id) { return Instance.libraryFor(context, id).getParentFile(); }
    File internal(Context context) { return internal(context, id); }
    File work(Context context) { return new File(internal(context), "build"); }
    /** Markers of finished steps whose output survives a rebuild (the extracted game files). */
    File keptMarkers(Context context) { return new File(internal(context), "done"); }
    File gameRoot(Context context) { return new File(Instance.instancesRoot(context), id); }
    private static File file(Context context, String id) { return new File(internal(context, id), "build.json"); }

    void save(Context context) throws IOException {
        JSONObject o = new JSONObject();
        try {
            o.put("profile", profileId);
            o.put("source", source.toString());
            o.put("pick", pick);
            o.put("name", name);
            o.put("title_id", titleId);
            o.put("state", state);
            o.put("error_code", errorCode);
            o.put("error_detail", errorDetail);
            o.put("failed_step", failedStep);
            o.put("stamp", stamp);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        File f = file(context, id);
        //noinspection ResultOfMethodCallIgnored
        f.getParentFile().mkdirs();
        File tmp = new File(f.getPath() + ".tmp");
        Files.write(tmp.toPath(), o.toString().getBytes(StandardCharsets.UTF_8));
        if (!tmp.renameTo(f)) throw new IOException("can't write " + f);
    }

    static BuildJob load(Context context, String id) {
        File f = file(context, id);
        if (!f.isFile()) return null;
        try {
            JSONObject o = new JSONObject(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8));
            BuildJob j = new BuildJob(id);
            j.profileId = o.optString("profile");
            j.source = Uri.parse(o.optString("source"));
            j.pick = o.optString("pick", "file");
            j.name = o.optString("name");
            j.titleId = o.optString("title_id");
            j.state = o.optString("state", RUNNING);
            j.errorCode = o.optString("error_code");
            j.errorDetail = o.optString("error_detail");
            j.failedStep = o.optString("failed_step");
            JSONObject s = o.optJSONObject("stamp");
            if (s != null) j.stamp = s;
            return j;
        } catch (IOException | JSONException e) {
            android.util.Log.w("XDroid360", "build.json of " + id + ": " + e);
            return null;
        }
    }

    /** The builds that haven't finished: running, interrupted (killed while running), failed. */
    static List<BuildJob> unfinished(Context context) {
        List<BuildJob> out = new ArrayList<>();
        File[] dirs = new File(context.getFilesDir(), "instances").listFiles();
        if (dirs == null) return out;
        java.util.Arrays.sort(dirs);
        for (File d : dirs) {
            BuildJob j = load(context, d.getName());
            if (j != null && !DONE.equals(j.state)) out.add(j);
        }
        return out;
    }

    /** Removes everything of this build: its game files, its scratch folder, its job. */
    void delete(Context context) {
        Instance.deleteTree(gameRoot(context));
        Instance.deleteTree(internal(context));
    }
}
