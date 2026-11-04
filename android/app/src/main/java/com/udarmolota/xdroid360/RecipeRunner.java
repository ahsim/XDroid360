package com.udarmolota.xdroid360;

import android.content.Context;
import android.os.ParcelFileDescriptor;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Writer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Runs a recipe's steps in order (SD1/toolchain/RECIPE_FORMAT_DRAFT.md): substitutes the
 * variables, starts the tools from nativeLibraryDir as child processes (never through a shell),
 * reports progress, and supports cancel and resume. A finished step leaves a marker; on resume
 * the marked steps are skipped, and extract and compile steps also resume per file.
 */
final class RecipeRunner {
    interface Listener {
        /** overall 0..1; step is the step's label, detail its own progress ("12 / 203"). */
        void onProgress(double overall, String step, String detail);
    }

    private final Context context;
    private final BuildJob job;
    private final Recipe recipe;
    private final Listener listener;
    private final AtomicBoolean cancelled = new AtomicBoolean();
    private final List<Process> running = Collections.synchronizedList(new ArrayList<>());
    private final Recipe.Vars vars = new Recipe.Vars();
    private final Map<String, List<String>> objects = new HashMap<>();
    private final File work, logs;
    private final int jobs;
    private double weightDone, weightTotal;
    private String stepLabel = "";

    RecipeRunner(Context context, BuildJob job, Recipe recipe, Listener listener) {
        this.context = context;
        this.job = job;
        this.recipe = recipe;
        this.listener = listener;
        this.work = job.work(context);
        this.logs = new File(work, "logs");
        this.jobs = jobs(recipe);
        vars.put("TOOLS", Toolchain.tools(context).getPath());
        vars.put("KIT", Toolchain.kitDir(context).getPath());
        vars.put("PROFILE", Toolchain.profileDir(context, recipe.profileId).getPath());
        vars.put("GAME", job.gameRoot(context).getPath());
        vars.put("WORK", work.getPath());
        vars.put("INSTANCE", job.internal(context).getPath());
        vars.put("JOBS", String.valueOf(jobs));
        vars.putAll(recipe.json.optJSONObject("vars"));
    }

    void cancel() {
        cancelled.set(true);
        synchronized (running) {
            for (Process p : running) p.destroyForcibly();
        }
    }

    /** Builds the library and installs it in the instance's internal folder. */
    void run() throws Recipe.Failure {
        JSONArray steps = recipe.json.optJSONArray("steps");
        for (int i = 0; i < steps.length(); i++) weightTotal += Math.max(0, steps.optJSONObject(i).optDouble("weight", 1));
        if (weightTotal <= 0) weightTotal = 1;
        //noinspection ResultOfMethodCallIgnored
        new File(work, "tmp").mkdirs();
        //noinspection ResultOfMethodCallIgnored
        logs.mkdirs();
        for (int i = 0; i < steps.length(); i++) {
            JSONObject step = steps.optJSONObject(i);
            String id = step.optString("id");
            stepLabel = step.optString("label", stepLabel);
            double weight = Math.max(0, step.optDouble("weight", 1));
            try {
                if ("each".equals(step.optString("kind"))) planObjects(step); // for "@objects:" even when skipped
                if (!marker(step).isFile() && applies(step)) {
                    for (String dir : strings(step.optJSONArray("creates"))) {
                        //noinspection ResultOfMethodCallIgnored
                        new File(vars.text(dir)).mkdirs();
                    }
                    runStep(step, weight);
                    writeMarker(step);
                }
            } catch (Recipe.Failure f) {
                job.failedStep = id;
                if ("cancelled".equals(f.code) || cancelled.get()) throw new Recipe.Failure("cancelled", "");
                String message = step.optString("message");
                if (Recipe.isCode(message) && !"not_enough_space".equals(f.code)) throw new Recipe.Failure(message, f.getMessage());
                throw f;
            } catch (IOException e) {
                job.failedStep = id;
                if (cancelled.get()) throw new Recipe.Failure("cancelled", "");
                throw new Recipe.Failure(noSpace(e) ? "not_enough_space" : defaultCode(step), e.toString());
            }
            weightDone += weight;
            report(0, "");
        }
        install();
    }

    // ---------------------------------------------------------------- steps

    private void runStep(JSONObject step, double weight) throws Recipe.Failure, IOException {
        switch (step.optString("kind")) {
            case "extract": extract(step, weight); break;
            case "run": runTool(step, weight); break;
            case "each": each(step, weight); break;
            case "write": write(step); break;
            case "verify": verify(step); break;
            default: throw new Recipe.Failure("tool_failed", "unknown step kind " + step.optString("kind"));
        }
    }

    private void extract(JSONObject step, double weight) throws Recipe.Failure, IOException {
        File to = new File(vars.text(step.optString("to", "${GAME}")));
        if ("xiso".equals(step.optString("format"))) {
            try (ParcelFileDescriptor pfd = GameSource.open(context, job.source);
                 FileInputStream in = new FileInputStream(pfd.getFileDescriptor());
                 FileChannel channel = in.getChannel()) {
                Xiso iso = new Xiso(channel);
                // Both games keep the disc's files in ${GAME}/game (the format's layout).
                iso.extract(new File(to, "game"), (done, total) -> {
                    report(total > 0 ? weight * done / total : 0, mb(done) + " / " + mb(total));
                    return !cancelled.get();
                });
            }
            return;
        }
        if (!"stfs".equals(step.optString("format"))) throw new Recipe.Failure("extract_failed", "unknown format");
        if (!StfsExtractor.available()) throw new Recipe.Failure("extract_failed", "no STFS extractor in this launcher");
        GameSource source = GameSource.detect(context, job.source, job.pick);
        List<GameSource.Package> packages = new ArrayList<>(source.packages);
        packages.sort((a, b) -> a.uri.toString().compareTo(b.uri.toString()));
        long total = 0, before = 0;
        for (GameSource.Package p : packages) total += p.size;
        for (int n = 0; n < packages.size(); n++) {
            GameSource.Package p = packages.get(n);
            File done = new File(job.keptMarkers(context), step.optString("id") + "." + n);
            if (!done.isFile()) {
                final long base = before, all = total;
                try (ParcelFileDescriptor pfd = GameSource.open(context, p.uri)) {
                    int code = StfsExtractor.nativeExtract(pfd.getFd(), to.getPath(), (d, t) -> {
                        report(all > 0 ? weight * (base + d) / all : 0, mb(base + d) + " / " + mb(all));
                        return !cancelled.get();
                    });
                    if (code != 0) {
                        String c = code < StfsExtractor.ERRORS.length ? StfsExtractor.ERRORS[code] : "extract_failed";
                        throw new Recipe.Failure(c, "STFS package " + p.uri.getLastPathSegment() + ": code " + code);
                    }
                }
                touch(done);
            }
            before += p.size;
        }
    }

    private void runTool(JSONObject step, double weight) throws Recipe.Failure, IOException {
        List<String> argv = command(step, step.optJSONArray("args"), null);
        File cwd = step.has("cwd") ? new File(vars.text(step.optString("cwd"))) : work;
        Pattern progress = step.has("progress") ? Pattern.compile(step.optString("progress")) : null;
        int code = exec(argv, cwd, new File(logs, step.optString("id") + ".log"), line -> {
            if (progress == null) return;
            Matcher m = progress.matcher(line);
            if (m.find()) {
                try {
                    double pct = Math.min(100, Math.max(0, Double.parseDouble(m.group(1))));
                    report(weight * pct / 100, String.format(java.util.Locale.ROOT, "%.0f%%", pct));
                } catch (NumberFormatException ignored) {
                }
            }
        });
        if (code != 0) throw new Recipe.Failure(defaultCode(step), step.optString("id") + " exited with " + code);
    }

    /** One step's inputs, in the order "@objects:" lists their objects. */
    private final Map<String, List<File>> inputs = new HashMap<>();

    private void planObjects(JSONObject step) throws Recipe.Failure {
        JSONObject in = step.optJSONObject("inputs");
        List<File> files = new ArrayList<>();
        if (in != null && in.has("glob")) files.addAll(glob(vars.text(in.optString("glob"))));
        if (in != null) for (String plus : strings(in.optJSONArray("plus"))) files.add(new File(vars.text(plus)));
        File dir = objectsDir(step);
        List<String> out = new ArrayList<>();
        for (File f : files) out.add(new File(dir, objectName(f)).getPath());
        inputs.put(step.optString("id"), files);
        objects.put(step.optString("id"), out);
    }

    private void each(JSONObject step, double weight) throws Recipe.Failure, IOException {
        String id = step.optString("id");
        List<File> files = new ArrayList<>(inputs.get(id));
        File dir = objectsDir(step);
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        File stepLogs = new File(logs, id);
        //noinspection ResultOfMethodCallIgnored
        stepLogs.mkdirs();
        JSONObject hints = step.optJSONObject("cost_hints");
        Map<File, Double> cost = new HashMap<>();
        double costTotal = 0;
        for (File f : files) {
            double c = hints != null && hints.has(f.getName()) ? hints.optDouble(f.getName(), 1) : 1;
            cost.put(f, c);
            costTotal += c;
        }
        if ("longest_first".equals(step.optString("schedule"))) {
            files.sort((a, b) -> {
                int byHint = Double.compare(cost.get(b), cost.get(a));
                return byHint != 0 ? byHint : Long.compare(b.length(), a.length());
            });
        }
        // Resume: an object that exists is done (each is written to .tmp and renamed).
        AtomicLong doneCount = new AtomicLong();
        double[] doneCost = { 0 };
        List<File> todo = new ArrayList<>();
        for (File f : files) {
            if (new File(dir, objectName(f)).isFile()) {
                doneCount.incrementAndGet();
                doneCost[0] += cost.get(f);
            } else {
                todo.add(f);
            }
        }
        final double total = costTotal;
        final int count = files.size();
        boolean stopOnFailure = !"continue".equals(step.optString("fail", "stop"));
        AtomicReference<Recipe.Failure> failure = new AtomicReference<>();
        ExecutorService pool = Executors.newFixedThreadPool(Math.max(1, Math.min(jobs, Math.max(1, todo.size()))));
        List<Future<?>> futures = new ArrayList<>();
        for (File f : todo) {
            futures.add(pool.submit(() -> {
                if (cancelled.get() || (stopOnFailure && failure.get() != null)) return;
                File obj = new File(dir, objectName(f));
                File tmp = new File(obj.getPath() + ".tmp");
                Map<String, String> extra = new HashMap<>();
                extra.put("IN", f.getPath());
                extra.put("OBJ", tmp.getPath());
                try {
                    List<String> argv = command(step, step.optJSONArray("args"), extra);
                    int code = exec(argv, work, new File(stepLogs, f.getName() + ".log"), null);
                    if (code != 0 || !tmp.isFile()) {
                        //noinspection ResultOfMethodCallIgnored
                        tmp.delete();
                        if (!cancelled.get()) {
                            failure.compareAndSet(null, new Recipe.Failure(defaultCode(step), f.getName() + " exited with " + code));
                            if (stopOnFailure) cancelProcesses();
                        }
                        return;
                    }
                    if (!tmp.renameTo(obj)) throw new IOException("can't rename " + tmp);
                    synchronized (doneCost) {
                        doneCost[0] += cost.get(f);
                        long n = doneCount.incrementAndGet();
                        report(weight * (total > 0 ? doneCost[0] / total : (double) n / count), n + " / " + count);
                    }
                } catch (Recipe.Failure e) {
                    failure.compareAndSet(null, e);
                } catch (IOException e) {
                    failure.compareAndSet(null, new Recipe.Failure(noSpace(e) ? "not_enough_space" : defaultCode(step), e.toString()));
                }
            }));
        }
        pool.shutdown();
        try {
            for (Future<?> fu : futures) fu.get();
            pool.awaitTermination(1, TimeUnit.MINUTES);
        } catch (InterruptedException | java.util.concurrent.ExecutionException e) {
            cancel();
            throw new Recipe.Failure("cancelled", e.toString());
        } finally {
            // Cancel: the jobs that were killed leave .tmp files.
            File[] left = dir.listFiles((d, n) -> n.endsWith(".tmp"));
            if (left != null) for (File t : left) //noinspection ResultOfMethodCallIgnored
                t.delete();
        }
        if (cancelled.get()) throw new Recipe.Failure("cancelled", "");
        if (failure.get() != null) throw failure.get();
    }

    private void write(JSONObject step) throws Recipe.Failure, IOException {
        File path = new File(vars.text(step.optString("path")));
        String text = step.has("from")
                ? vars.text(new String(Files.readAllBytes(new File(vars.text(step.optString("from"))).toPath()), StandardCharsets.UTF_8))
                : vars.text(step.optString("text"));
        //noinspection ResultOfMethodCallIgnored
        path.getParentFile().mkdirs();
        Files.write(path.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private void verify(JSONObject step) throws Recipe.Failure, IOException {
        JSONObject files = step.optJSONObject("files");
        if (files == null) return;
        for (Iterator<String> it = files.keys(); it.hasNext(); ) {
            String path = it.next();
            File f = new File(vars.text(path));
            if (!f.isFile()) throw new Recipe.Failure(defaultCode(step), f + " is missing");
            String sha = sha256(f);
            boolean ok = false;
            for (String accepted : strings(files.optJSONArray(path))) ok |= accepted.equalsIgnoreCase(sha);
            if (!ok) throw new Recipe.Failure(defaultCode(step), f.getName() + " has SHA-256 " + sha);
        }
    }

    /** Checks the library's ABI note, then moves it into the instance and drops the scratch files. */
    private void install() throws Recipe.Failure {
        JSONObject result = recipe.json.optJSONObject("result");
        if (result == null) throw new Recipe.Failure("link_failed", "the recipe has no result");
        File library = new File(vars.text(result.optString("library")));
        File target = new File(vars.text(result.optString("install", "${INSTANCE}/libgame.so")));
        if (!library.isFile()) throw new Recipe.Failure("link_failed", "no " + library.getName() + " after the link");
        try {
            int abi = ElfNote.abi(library);
            if (abi != recipe.abi()) throw new Recipe.Failure("abi_mismatch", "the library is for ABI " + abi + ", the recipe says " + recipe.abi());
            //noinspection ResultOfMethodCallIgnored
            target.delete();
            if (!library.renameTo(target)) throw new IOException("can't move the library to " + target);
        } catch (IOException e) {
            throw new Recipe.Failure(noSpace(e) ? "not_enough_space" : "link_failed", e.toString());
        }
        List<File> keep = new ArrayList<>();
        for (String k : strings(result.optJSONArray("keep"))) keep.add(new File(vars.text(k)));
        File[] children = work.listFiles();
        if (children != null) {
            for (File c : children) if (!keep.contains(c)) Instance.deleteTree(c);
        }
    }

    // ---------------------------------------------------------------- helpers

    private interface Lines {
        void line(String line);
    }

    private List<String> command(JSONObject step, JSONArray args, Map<String, String> extra) throws Recipe.Failure {
        String tool = step.optString("tool");
        if (!tool.matches("[a-z0-9_]+")) throw new Recipe.Failure("tool_failed", "bad tool name " + tool);
        File exe = new File(Toolchain.tools(context), "lib" + tool + ".so");
        if (!exe.isFile()) throw new Recipe.Failure("tool_failed", "this launcher has no " + exe.getName());
        List<String> argv = new ArrayList<>();
        argv.add(exe.getPath());
        argv.addAll(vars.args(args, extra, objects));
        return argv;
    }

    /** Runs argv with stdout and stderr in log; lines also go to lines (when given). */
    private int exec(List<String> argv, File cwd, File log, Lines lines) throws IOException, Recipe.Failure {
        if (cancelled.get()) throw new Recipe.Failure("cancelled", "");
        ProcessBuilder pb = new ProcessBuilder(argv).directory(cwd).redirectErrorStream(true);
        pb.environment().put("TMPDIR", new File(work, "tmp").getPath());
        Process p = pb.start();
        running.add(p);
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8));
             Writer w = new FileWriter(log)) {
            w.write(String.join(" ", argv) + "\n\n");
            String line;
            while ((line = r.readLine()) != null) {
                w.write(line);
                w.write('\n');
                if (lines != null) lines.line(line);
            }
            return p.waitFor();
        } catch (InterruptedException e) {
            p.destroyForcibly();
            throw new Recipe.Failure("cancelled", "");
        } finally {
            running.remove(p);
        }
    }

    private void cancelProcesses() {
        synchronized (running) {
            for (Process p : running) p.destroyForcibly();
        }
    }

    private boolean applies(JSONObject step) throws Recipe.Failure {
        JSONObject when = step.optJSONObject("when");
        if (when == null) return true;
        if (when.has("exists")) return new File(vars.text(when.optString("exists"))).exists();
        return true;
    }

    private File marker(JSONObject step) {
        String id = step.optString("id");
        return step.optBoolean("keeps_game") ? new File(job.keptMarkers(context), id) : new File(new File(work, "done"), id);
    }

    private void writeMarker(JSONObject step) throws IOException {
        touch(marker(step));
    }

    private static void touch(File f) throws IOException {
        //noinspection ResultOfMethodCallIgnored
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), new byte[0]);
    }

    private File objectsDir(JSONObject step) throws Recipe.Failure {
        return new File(vars.text(step.optString("objects", "${WORK}/obj/" + step.optString("id"))));
    }

    private static String objectName(File input) {
        String n = input.getName();
        int dot = n.lastIndexOf('.');
        return (dot > 0 ? n.substring(0, dot) : n) + ".o";
    }

    /** A glob in the file name only ("dir/ppc_recomp.*.cpp"), sorted by name. */
    private static List<File> glob(String pattern) {
        File p = new File(pattern);
        File dir = p.getParentFile();
        StringBuilder re = new StringBuilder();
        for (char c : p.getName().toCharArray()) {
            if (c == '*') re.append(".*");
            else if (c == '?') re.append('.');
            else re.append(Pattern.quote(String.valueOf(c)));
        }
        Pattern name = Pattern.compile(re.toString());
        List<File> out = new ArrayList<>();
        File[] files = dir != null ? dir.listFiles() : null;
        if (files != null) for (File f : files) if (f.isFile() && name.matcher(f.getName()).matches()) out.add(f);
        out.sort((a, b) -> a.getName().compareTo(b.getName()));
        return out;
    }

    private static List<String> strings(JSONArray a) {
        List<String> out = new ArrayList<>();
        if (a != null) for (int i = 0; i < a.length(); i++) out.add(a.optString(i));
        return out;
    }

    private static String defaultCode(JSONObject step) {
        String m = step.optString("message");
        if (Recipe.isCode(m)) return m;
        switch (step.optString("kind")) {
            case "extract": return "extract_failed";
            case "each": return "compile_failed";
            case "verify": return "profile_mismatch";
            default: return "tool_failed";
        }
    }

    private static boolean noSpace(IOException e) {
        String m = String.valueOf(e.getMessage());
        return m.contains("ENOSPC") || m.contains("No space left");
    }

    private static String sha256(File f) throws IOException {
        try {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            try (FileInputStream in = new FileInputStream(f)) {
                byte[] b = new byte[1 << 16];
                int n;
                while ((n = in.read(b)) > 0) sha.update(b, 0, n);
            }
            StringBuilder s = new StringBuilder();
            for (byte x : sha.digest()) s.append(String.format("%02x", x));
            return s.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String mb(long bytes) {
        return (bytes >> 20) + " MB";
    }

    private void report(double withinStep, String detail) {
        listener.onProgress(Math.min(1, (weightDone + withinStep) / weightTotal), stepLabel, detail);
    }

    /** ${JOBS}: min(cores, (MemAvailable - reserve_mb) / max_rss_mb), at least 1. */
    private static int jobs(Recipe recipe) {
        int cores = Runtime.getRuntime().availableProcessors();
        JSONObject j = recipe.json.optJSONObject("jobs");
        long rss = j != null ? j.optLong("max_rss_mb", 600) : 600, reserve = j != null ? j.optLong("reserve_mb", 1024) : 1024;
        long available = memAvailableMb();
        int byMemory = available > 0 && rss > 0 ? (int) ((available - reserve) / rss) : cores;
        return Math.max(1, Math.min(cores, byMemory));
    }

    private static long memAvailableMb() {
        try (BufferedReader r = new BufferedReader(new java.io.FileReader("/proc/meminfo"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("MemAvailable:")) return Long.parseLong(line.replaceAll("[^0-9]", "")) / 1024;
            }
        } catch (IOException | NumberFormatException ignored) {
        }
        return -1;
    }
}
