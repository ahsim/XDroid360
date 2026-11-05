package com.udarmolota.xdroid360;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.os.PowerManager;

import androidx.core.app.NotificationCompat;
import androidx.core.app.ServiceCompat;

import java.io.File;
import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Builds a game on the phone in the foreground (with a notification), so the screen can go
 * off and the player can leave the launcher: the recipe's steps, then the new instance. One
 * build at a time. The build screen reads status(id); a build Android killed is found again
 * by its build.json (BuildJob.unfinished) and continues from its last finished step.
 */
public class BuildService extends Service {
    static final String EXTRA_ID = "id";
    static final String ACTION_CANCEL = "com.udarmolota.xdroid360.CANCEL_BUILD";
    private static final String CHANNEL = "build";
    private static final int NOTIFICATION = 1;

    /** What the build screen shows. */
    static final class Status {
        volatile double overall;
        volatile String step = "";
        volatile String detail = "";
        volatile boolean finished;
    }

    private static final Map<String, Status> statuses = new ConcurrentHashMap<>();
    private static volatile String runningId;
    /** Asked to start, its onStartCommand not run yet: running, as far as the build screen goes. */
    private static volatile String startingId;
    private RecipeRunner runner;
    private PowerManager.WakeLock wakeLock;
    private long lastNotified;

    static Status status(String id) { return statuses.get(id); }

    /** Whether this process is building id right now (else a "running" build.json was interrupted). */
    static boolean isRunning(String id) { return id.equals(runningId) || id.equals(startingId); }

    static boolean busy() { return runningId != null || startingId != null; }

    static void start(Context context, String id) {
        startingId = id;
        androidx.core.content.ContextCompat.startForegroundService(context,
                new Intent(context, BuildService.class).putExtra(EXTRA_ID, id));
    }

    static void cancel(Context context, String id) {
        context.startService(new Intent(context, BuildService.class).setAction(ACTION_CANCEL).putExtra(EXTRA_ID, id));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String id = intent != null ? intent.getStringExtra(EXTRA_ID) : null;
        if (intent != null && ACTION_CANCEL.equals(intent.getAction())) {
            RecipeRunner r = runner;
            if (r != null && id != null && id.equals(runningId)) r.cancel();
            return START_NOT_STICKY;
        }
        if (id == null || runningId != null) {
            if (id != null && id.equals(startingId) && !id.equals(runningId)) startingId = null;
            if (runningId == null) stopSelf(startId);
            return START_NOT_STICKY;
        }
        BuildJob job = BuildJob.load(this, id);
        if (job == null) {
            startingId = null;
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        createChannel();
        ServiceCompat.startForeground(this, NOTIFICATION, notification(job, getString(R.string.build_preparing), 0, true),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        runningId = id;
        startingId = null;
        Status status = new Status();
        statuses.put(id, status);
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "XDroid360:build");
        wakeLock.acquire(6 * 60 * 60 * 1000L);
        new Thread(() -> build(job, status), "build " + id).start();
        return START_NOT_STICKY;
    }

    private void build(BuildJob job, Status status) {
        try {
            Recipe recipe = Toolchain.profile(this, job.profileId);
            if (recipe == null) throw new Recipe.Failure("unknown_title", "no profile " + job.profileId + " in this launcher");
            String mismatch = Toolchain.mismatch(this, recipe);
            if (mismatch != null) throw new Recipe.Failure("profile_mismatch", mismatch);
            // Another recipe, kit or tools than the build started with: its scratch files are void
            // (the extracted game files stay, by their own markers).
            if (!recipe.stamp().toString().equals(job.stamp.toString())) {
                Instance.deleteTree(job.work(this));
                job.stamp = recipe.stamp();
            }
            job.state = BuildJob.RUNNING;
            job.errorCode = job.errorDetail = job.failedStep = "";
            job.save(this);
            Toolchain.unpack(this, recipe);
            runner = new RecipeRunner(this, job, recipe, (overall, step, detail) -> {
                status.overall = overall;
                status.step = step;
                status.detail = detail;
                long now = System.currentTimeMillis();
                if (now - lastNotified > 1000) {
                    lastNotified = now;
                    notify(job, step + (detail.isEmpty() ? "" : " · " + detail), overall, true);
                }
            });
            runner.run();
            finish(job, recipe);
            job.state = BuildJob.DONE;
            job.save(this);
            notify(job, getString(R.string.build_done), 1, false);
        } catch (Recipe.Failure f) {
            job.state = "cancelled".equals(f.code) ? BuildJob.CANCELLED : BuildJob.FAILED;
            job.errorCode = f.code;
            job.errorDetail = String.valueOf(f.getMessage());
            android.util.Log.w("XDroid360", "build " + job.id + " " + f.code + " at " + job.failedStep + ": " + f.getMessage());
            saveQuietly(job);
            if (!BuildJob.CANCELLED.equals(job.state)) notify(job, BuildErrors.text(this, f.code), 0, false);
        } catch (IOException | RuntimeException e) {
            job.state = BuildJob.FAILED;
            job.errorCode = "tool_failed";
            job.errorDetail = e.toString();
            android.util.Log.e("XDroid360", "build " + job.id, e);
            saveQuietly(job);
            notify(job, BuildErrors.text(this, job.errorCode), 0, false);
        } finally {
            status.finished = true;
            runner = null;
            runningId = null;
            if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
            ServiceCompat.stopForeground(this, BuildJob.CANCELLED.equals(job.state)
                    ? ServiceCompat.STOP_FOREGROUND_REMOVE : ServiceCompat.STOP_FOREGROUND_DETACH);
            stopSelf();
        }
    }

    /** The new instance: its instance.json, a unique name, a warm shader cache. */
    private void finish(BuildJob job, Recipe recipe) throws IOException {
        Instance instance = Instance.built(this, job.id, job.titleId, recipe.title(), recipe.stamp());
        //noinspection ResultOfMethodCallIgnored
        instance.userdata().mkdirs();
        Instance warm = InstanceActions.warmestOther(Instance.list(this), instance);
        if (warm != null) {
            try {
                InstanceActions.copyShaderCache(warm, instance);
            } catch (IOException e) {
                android.util.Log.w("XDroid360", "shader cache from " + warm.id + ": " + e);
            }
        }
        String name = job.name == null || job.name.trim().isEmpty() ? recipe.title() : job.name.trim();
        instance.name = Instance.uniqueName(Instance.list(this), name);
        instance.save(); // last: an instance without instance.json isn't listed
    }

    private void saveQuietly(BuildJob job) {
        try {
            job.save(this);
        } catch (IOException e) {
            android.util.Log.w("XDroid360", "build.json of " + job.id + ": " + e);
        }
    }

    // ---------------------------------------------------------------- notification

    private void createChannel() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, getString(R.string.build_channel),
                    NotificationManager.IMPORTANCE_LOW));
        }
    }

    private Notification notification(BuildJob job, String text, double progress, boolean ongoing) {
        Intent open = new Intent(this, BuildActivity.class).putExtra(BuildActivity.EXTRA_JOB, job.id)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        NotificationCompat.Builder b = new NotificationCompat.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle(job.name)
                .setContentText(text)
                .setOnlyAlertOnce(true)
                .setOngoing(ongoing)
                .setContentIntent(PendingIntent.getActivity(this, job.id.hashCode(), open,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        if (ongoing) {
            b.setProgress(1000, (int) (progress * 1000), false);
            Intent cancel = new Intent(this, BuildService.class).setAction(ACTION_CANCEL).putExtra(EXTRA_ID, job.id);
            b.addAction(0, getString(R.string.build_cancel), PendingIntent.getService(this, 1, cancel,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }
        return b.build();
    }

    private void notify(BuildJob job, String text, double progress, boolean ongoing) {
        getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(job, text, progress, ongoing));
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    /** The newest log the build wrote (the failed step's or file's, after a failure). */
    static File newestLog(Context context, String id) {
        File logs = new File(BuildJob.internal(context, id), "build/logs");
        return newest(logs);
    }

    private static File newest(File dir) {
        File best = null;
        File[] files = dir.listFiles();
        if (files == null) return null;
        for (File f : files) {
            File c = f.isDirectory() ? newest(f) : f;
            if (c != null && (best == null || c.lastModified() > best.lastModified())) best = c;
        }
        return best;
    }
}
