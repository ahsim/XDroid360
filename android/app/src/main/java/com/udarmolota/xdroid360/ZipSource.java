package com.udarmolota.xdroid360;

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * A game the player has as a .zip: Dragon Age: Origins' disc image (.iso) inside it, or State of
 * Decay's console folder (<title ID>/000D0000/..., 00000002/..., 000B0000/...). Both are found at
 * any depth ("State Of Decay/584111E8/000D0000/..." as well as "584111E8/000D0000/..."). A zip
 * entry isn't seekable, so what the extractors read is inflated to a real file first
 * (inflate()); the central directory is read in place through the picked file's descriptor.
 */
final class ZipSource implements Closeable {
    /** An STFS package in a console folder: the title ID folder, then the content type folder. */
    private static final Pattern PACKAGE =
            Pattern.compile("(?:^|/)([0-9A-Fa-f]{8})/(000D0000|00000002|000B0000)/[^/]+$");

    interface Progress {
        /** false cancels. */
        boolean onProgress(long done, long total);
    }

    private final ParcelFileDescriptor pfd;
    private final ZipFile zip;

    private ZipSource(ParcelFileDescriptor pfd, ZipFile zip) {
        this.pfd = pfd;
        this.zip = zip;
    }

    /** Whether the file starts like a zip ("PK\3\4"). */
    static boolean looksLikeZip(Context context, Uri uri) {
        try (ParcelFileDescriptor pfd = GameSource.open(context, uri);
             FileInputStream in = new FileInputStream(pfd.getFileDescriptor())) {
            byte[] b = new byte[4];
            return in.read(b) == 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4;
        } catch (IOException | Recipe.Failure e) {
            return false;
        }
    }

    static ZipSource open(Context context, Uri uri) throws Recipe.Failure {
        ParcelFileDescriptor pfd = GameSource.open(context, uri);
        try {
            // The picked file through its descriptor: a real file, opened again by path.
            return new ZipSource(pfd, new ZipFile(new File("/proc/self/fd/" + pfd.getFd())));
        } catch (IOException e) {
            try {
                pfd.close();
            } catch (IOException ignored) {
            }
            throw new Recipe.Failure("source_not_seekable", "the zip can't be opened in place: " + e);
        }
    }

    /** The largest .iso in the zip, or null. */
    ZipEntry iso() {
        ZipEntry best = null;
        for (Enumeration<? extends ZipEntry> it = zip.entries(); it.hasMoreElements(); ) {
            ZipEntry e = it.nextElement();
            if (!e.isDirectory() && e.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".iso")
                    && (best == null || e.getSize() > best.getSize())) best = e;
        }
        return best;
    }

    /** The STFS packages in the zip (game, DLC, title update), sorted by name. */
    List<ZipEntry> packages() {
        List<ZipEntry> out = new ArrayList<>();
        for (Enumeration<? extends ZipEntry> it = zip.entries(); it.hasMoreElements(); ) {
            ZipEntry e = it.nextElement();
            if (!e.isDirectory() && PACKAGE.matcher(e.getName()).find()) out.add(e);
        }
        out.sort((a, b) -> a.getName().compareTo(b.getName()));
        return out;
    }

    /** A package's title ID folder (upper case). */
    static String titleId(ZipEntry e) {
        Matcher m = PACKAGE.matcher(e.getName());
        return m.find() ? m.group(1).toUpperCase(java.util.Locale.ROOT) : "";
    }

    /** A package's content type folder ("000D0000" for the game). */
    static String contentType(ZipEntry e) {
        Matcher m = PACKAGE.matcher(e.getName());
        return m.find() ? m.group(2) : "";
    }

    ZipEntry entry(String name) { return zip.getEntry(name); }

    /** The first max bytes of an entry, inflated to out (headers, for a probe). */
    void inflateHead(ZipEntry e, File out, int max) throws IOException {
        try (InputStream in = zip.getInputStream(e); OutputStream o = new FileOutputStream(out)) {
            byte[] b = new byte[1 << 16];
            int left = max, n;
            while (left > 0 && (n = in.read(b, 0, Math.min(b.length, left))) > 0) {
                o.write(b, 0, n);
                left -= n;
            }
        }
    }

    /**
     * Inflates an entry to out, through out.part, renamed when complete. A complete out (its
     * size the entry's) is kept, so an interrupted build doesn't inflate it again.
     */
    void inflate(ZipEntry e, File out, Progress progress) throws IOException, Recipe.Failure {
        if (out.isFile() && e.getSize() >= 0 && out.length() == e.getSize()) {
            if (!progress.onProgress(e.getSize(), e.getSize())) throw new Recipe.Failure("cancelled", "");
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        out.getParentFile().mkdirs();
        File part = new File(out.getPath() + ".part");
        boolean ok = false;
        try (InputStream in = zip.getInputStream(e); OutputStream o = new FileOutputStream(part)) {
            byte[] b = new byte[1 << 20];
            long done = 0, total = e.getSize();
            int n;
            while ((n = in.read(b)) > 0) {
                o.write(b, 0, n);
                done += n;
                if (!progress.onProgress(done, total)) throw new Recipe.Failure("cancelled", "");
            }
            ok = true;
        } catch (java.util.zip.ZipException damaged) {
            throw new Recipe.Failure("extract_failed", e.getName() + ": " + damaged.getMessage());
        } finally {
            //noinspection ResultOfMethodCallIgnored
            if (!ok) part.delete();
        }
        //noinspection ResultOfMethodCallIgnored
        out.delete();
        if (!part.renameTo(out)) throw new IOException("can't rename " + part);
    }

    @Override
    public void close() throws IOException {
        try {
            zip.close();
        } finally {
            pfd.close();
        }
    }
}
