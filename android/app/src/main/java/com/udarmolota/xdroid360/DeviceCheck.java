package com.udarmolota.xdroid360;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;
import android.system.Os;
import android.system.OsConstants;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The device check: what the phone's Vulkan offers Xenia's Vulkan backend, with each driver the
 * game could run on (the built-in Turnip, the phone's own, a game's installed one), and what
 * that means: the workarounds Xenia switches to, the effects it would skip, what stops it. The
 * rules are Xenia's own (xenia-vulkan: ui/vulkan/vulkan_device.cc, gpu/vulkan/*_cache.cc,
 * vulkan_command_processor.cc). Results are kept in files/devicecheck/ for the bug report.
 * Runs in DeviceCheckActivity's own process: a driver that crashes takes only the check down,
 * and the check says so next time (a .running file without its result).
 */
final class DeviceCheck {
    enum Level { OK, INFO, WORKAROUND, PROBLEM, FAIL }

    /** One driver to check. path: the driver .so, null for the phone's own. */
    static final class Target {
        final String key, label;
        final File path;

        Target(String key, String label, File path) {
            this.key = key;
            this.label = label;
            this.path = path;
        }
    }

    static final class Finding {
        final Level level;
        final String text;

        Finding(Level level, String text) {
            this.level = level;
            this.text = text;
        }
    }

    static final class Result {
        final Target target;
        final Map<String, String> values;
        final List<Finding> findings = new ArrayList<>();
        Level level = Level.OK;

        Result(Target target, Map<String, String> values) {
            this.target = target;
            this.values = values;
        }

        void add(Level l, String text) {
            findings.add(new Finding(l, text));
            if (l.ordinal() > level.ordinal()) level = l;
        }
    }

    private static boolean loaded;

    private static native String nativeProbe(String driverPath, String hookDir);

    private DeviceCheck() {
    }

    static File dir(Context c) {
        File d = new File(c.getFilesDir(), "devicecheck");
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();
        return d;
    }

    /** The built-in Turnip, the phone's own driver, and each game's installed driver. */
    static List<Target> targets(Context c) {
        List<Target> targets = new ArrayList<>();
        File builtin = new File(c.getApplicationInfo().nativeLibraryDir, GameActivity.BUILTIN_DRIVER);
        if (builtin.isFile()) targets.add(new Target("builtin", c.getString(R.string.check_driver_builtin), builtin));
        targets.add(new Target("system", c.getString(R.string.check_driver_system), null));
        for (Instance i : Instance.list(c)) {
            if (!Instance.DRIVER_CUSTOM.equals(i.driver)) continue;
            File driver = GameActivity.chosenDriver(c, i, i.root);
            if (driver != null) {
                targets.add(new Target("custom-" + i.id,
                        c.getString(R.string.check_driver_custom, i.name, driver.getName()), driver));
            }
        }
        return targets;
    }

    /** Checks target (on this thread; the driver is loaded into this process). */
    static Result run(Context c, Target t) {
        File running = new File(dir(c), t.key + ".running");
        File result = new File(dir(c), t.key + ".txt");
        String raw;
        try {
            //noinspection ResultOfMethodCallIgnored
            running.createNewFile();
            if (!loaded) {
                System.loadLibrary("daoprobe");
                loaded = true;
            }
            raw = nativeProbe(t.path != null ? t.path.getAbsolutePath() : null, c.getApplicationInfo().nativeLibraryDir);
        } catch (Throwable e) {
            raw = "error=" + e + "\n";
        }
        Result r = judge(c, t, parse(raw));
        write(result, report(c, r));
        //noinspection ResultOfMethodCallIgnored
        running.delete();
        return r;
    }

    /** A check that crashed last time (its process died while the driver was loaded), or null. */
    static Result crashed(Context c, Target t) {
        if (!new File(dir(c), t.key + ".running").isFile()) return null;
        Result r = new Result(t, new LinkedHashMap<>());
        r.add(Level.FAIL, c.getString(R.string.check_crashed));
        return r;
    }

    static void clearCrash(Context c, Target t) {
        //noinspection ResultOfMethodCallIgnored
        new File(dir(c), t.key + ".running").delete();
    }

    private static Map<String, String> parse(String raw) {
        Map<String, String> values = new LinkedHashMap<>();
        for (String line : raw.split("\n")) {
            int eq = line.indexOf('=');
            if (eq > 0) values.put(line.substring(0, eq), line.substring(eq + 1));
        }
        return values;
    }

    private static boolean yes(Map<String, String> v, String key) {
        return "1".equals(v.get(key));
    }

    /** Xenia's rules, as its code applies them, in a player's words. */
    private static Result judge(Context c, Target t, Map<String, String> v) {
        Result r = new Result(t, v);
        if (v.containsKey("error")) {
            r.add(Level.FAIL, c.getString(R.string.check_no_vulkan, v.get("error")));
            return r;
        }
        // Without these Xenia doesn't create its device (vulkan_device.cc).
        if (!yes(v, "feature.independentBlend")) r.add(Level.FAIL, c.getString(R.string.check_no_independent_blend));
        if (!yes(v, "ext.VK_KHR_swapchain")) r.add(Level.FAIL, c.getString(R.string.check_no_swapchain));

        // Effects Xenia skips without them (vulkan_command_processor.cc: memexport is dropped).
        if (!yes(v, "feature.vertexPipelineStoresAndAtomics"))
            r.add(Level.PROBLEM, c.getString(R.string.check_no_vertex_stores));
        if (!yes(v, "feature.fragmentStoresAndAtomics"))
            r.add(Level.PROBLEM, c.getString(R.string.check_no_fragment_stores));

        // Workarounds (vulkan_texture_cache.cc, vulkan_primitive_processor.cc).
        boolean bc = true;
        for (String f : new String[] { "BC1", "BC2", "BC3", "BC4", "BC5" }) bc &= yes(v, "format." + f);
        if (!bc) r.add(Level.WORKAROUND, c.getString(R.string.check_no_bc));
        if (!yes(v, "feature.geometryShader")) r.add(Level.WORKAROUND, c.getString(R.string.check_no_geometry));
        if (!yes(v, "format.D24S8")) r.add(Level.INFO, c.getString(R.string.check_no_d24s8));

        // Information (vulkan_render_target_cache.cc, vulkan_shared_memory.cc).
        boolean fsi = (yes(v, "feature.fragmentShaderSampleInterlock") || yes(v, "feature.fragmentShaderPixelInterlock"))
                && yes(v, "feature.fragmentStoresAndAtomics") && yes(v, "feature.sampleRateShading")
                && yes(v, "limit.standardSampleLocations");
        r.add(Level.INFO, c.getString(fsi ? R.string.check_rt_fsi_possible : R.string.check_rt_fbo));
        if (!yes(v, "feature.sparseResidencyBuffer")) r.add(Level.INFO, c.getString(R.string.check_no_sparse));
        if (!yes(v, "feature.samplerAnisotropy")) r.add(Level.INFO, c.getString(R.string.check_no_anisotropy));

        // The phone itself.
        long ram = totalRam(c);
        if (ram > 0 && ram < 7L * 1024 * 1024 * 1024) r.add(Level.WORKAROUND, c.getString(R.string.check_low_ram, gb(ram)));
        long page = Os.sysconf(OsConstants._SC_PAGESIZE);
        if (page != 4096) r.add(Level.WORKAROUND, c.getString(R.string.check_page_size, page / 1024));
        return r;
    }

    static long totalRam(Context c) {
        ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
        c.getSystemService(ActivityManager.class).getMemoryInfo(m);
        return m.totalMem;
    }

    private static String gb(long bytes) {
        return String.format(java.util.Locale.ROOT, "%.1f", bytes / (1024.0 * 1024 * 1024));
    }

    /** The phone: model, SoC, Android, RAM, page size, GPU as the kernel names it. */
    static String phone(Context c) {
        String gpu = "?";
        try (InputStream in = new FileInputStream("/sys/class/kgsl/kgsl-3d0/gpu_model")) {
            byte[] b = new byte[256];
            int n = in.read(b);
            if (n > 0) gpu = new String(b, 0, n, StandardCharsets.UTF_8).trim();
        } catch (IOException ignored) {
        }
        return Build.MANUFACTURER + " " + Build.MODEL
                + "\nSoC: " + (Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : "?")
                + "\nAndroid " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
                + "\nRAM: " + gb(totalRam(c)) + " GB, pages " + Os.sysconf(OsConstants._SC_PAGESIZE) / 1024 + " KB"
                + "\nGPU (kernel): " + gpu;
    }

    /** What the device check found, as the bug report's text. */
    private static String report(Context c, Result r) {
        StringBuilder s = new StringBuilder();
        s.append(r.target.label).append(": ").append(r.level).append('\n');
        if (r.target.path != null) s.append(r.target.path).append('\n');
        for (Finding f : r.findings) s.append("- ").append(f.level).append(": ").append(f.text).append('\n');
        s.append('\n').append(phone(c)).append("\n\n");
        for (Map.Entry<String, String> e : r.values.entrySet()) s.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        return s.toString();
    }

    /** The saved results, for the bug report: name → text. */
    static Map<String, byte[]> saved(Context c) {
        Map<String, byte[]> out = new LinkedHashMap<>();
        File[] files = dir(c).listFiles();
        if (files == null) return out;
        for (File f : files) {
            String name = f.getName();
            if (name.endsWith(".running")) {
                out.put("devicecheck-" + name.replace(".running", "") + "-crashed.txt",
                        "The device check crashed with this driver.\n".getBytes(StandardCharsets.UTF_8));
            } else if (name.endsWith(".txt")) {
                try (InputStream in = new FileInputStream(f)) {
                    byte[] b = new byte[(int) f.length()];
                    int n = in.read(b);
                    out.put("devicecheck-" + name, n == b.length ? b : java.util.Arrays.copyOf(b, Math.max(n, 0)));
                } catch (IOException ignored) {
                }
            }
        }
        return out;
    }

    private static void write(File f, String text) {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }
}
