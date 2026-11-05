package com.udarmolota.xdroid360;

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;
import java.util.zip.ZipEntry;

/**
 * A game the player has as a .zip: Dragon Age: Origins' disc image (.iso) inside it, or State of
 * Decay's console folder (<title ID>/000D0000/..., 00000002/..., 000B0000/...). Both are found at
 * any depth ("State Of Decay/584111E8/000D0000/...", "584111E8/000D0000/...", or the title
 * folder's content itself: "000D0000/..."; the packages' own headers say which title). A zip
 * entry isn't seekable, so what the extractors read is inflated to a real file first (inflate()).
 *
 * The zip is read through the picked file's own descriptor, by positional reads: its central
 * directory (with ZIP64, the games are over 4 GB), then each entry's data. Opening it again by
 * path (/proc/self/fd/N, which java.util.zip.ZipFile needs) is refused on shared storage.
 */
final class ZipSource implements Closeable {
    /** An STFS package in a console folder: the title ID folder (if any), then the content type folder. */
    private static final Pattern PACKAGE =
            Pattern.compile("(?:^|/)(?:([0-9A-Fa-f]{8})/)?(000D0000|00000002|000B0000)/[^/]+$");
    private static final int EOCD = 0x06054b50, ZIP64_EOCD = 0x06064b50, ZIP64_LOCATOR = 0x07064b50;
    private static final int CENTRAL = 0x02014b50, LOCAL = 0x04034b50;
    private static final int MAX_ENTRIES = 1 << 20;

    interface Progress {
        /** false cancels. */
        boolean onProgress(long done, long total);
    }

    private final ParcelFileDescriptor pfd;
    private final FileInputStream in;
    private final FileChannel channel;
    private final Map<String, ZipEntry> entries = new LinkedHashMap<>();
    /** Each entry's local header offset (ZipEntry has no field for it). */
    private final Map<String, Long> offsets = new HashMap<>();

    private ZipSource(ParcelFileDescriptor pfd) throws IOException, Recipe.Failure {
        this.pfd = pfd;
        this.in = new FileInputStream(pfd.getFileDescriptor());
        this.channel = in.getChannel();
        try {
            channel.position(0);
        } catch (IOException e) {
            throw new Recipe.Failure("source_not_seekable", e.toString());
        }
        readCentralDirectory();
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
            return new ZipSource(pfd);
        } catch (IOException | Recipe.Failure e) {
            try {
                pfd.close();
            } catch (IOException ignored) {
            }
            if (e instanceof Recipe.Failure) throw (Recipe.Failure) e;
            throw new Recipe.Failure("extract_failed", "the zip's folder table is damaged: " + e);
        }
    }

    // ---------------------------------------------------------------- the central directory

    private void readCentralDirectory() throws IOException, Recipe.Failure {
        long size = channel.size();
        // The end record is the last 22 bytes, or up to 64 KB earlier behind a comment.
        int tail = (int) Math.min(size, 22 + 0xFFFF);
        ByteBuffer t = read(size - tail, tail);
        int at = -1;
        for (int i = tail - 22; i >= 0; i--) {
            if (t.getInt(i) == EOCD) {
                at = i;
                break;
            }
        }
        if (at < 0) throw new Recipe.Failure("unknown_title", "not a zip (no end of central directory)");
        long count = t.getShort(at + 10) & 0xFFFF;
        long dirSize = t.getInt(at + 12) & 0xFFFFFFFFL;
        long dirOffset = t.getInt(at + 16) & 0xFFFFFFFFL;
        // ZIP64: a locator just before the end record points to the 64-bit end record.
        if (at >= 20 && t.getInt(at - 20) == ZIP64_LOCATOR) {
            long z64 = t.getLong(at - 20 + 8);
            ByteBuffer r = read(z64, 56);
            if (r.getInt(0) != ZIP64_EOCD) throw new Recipe.Failure("extract_failed", "damaged ZIP64 end record");
            count = r.getLong(32);
            dirSize = r.getLong(40);
            dirOffset = r.getLong(48);
        }
        if (count > MAX_ENTRIES || dirSize > Integer.MAX_VALUE || dirOffset + dirSize > size) {
            throw new Recipe.Failure("extract_failed", "the zip's folder table is damaged");
        }
        ByteBuffer d = read(dirOffset, (int) dirSize);
        int p = 0;
        for (long n = 0; n < count; n++) {
            if (p + 46 > d.limit() || d.getInt(p) != CENTRAL) throw new Recipe.Failure("extract_failed", "damaged zip entry");
            int method = d.getShort(p + 10) & 0xFFFF;
            long csize = d.getInt(p + 20) & 0xFFFFFFFFL, usize = d.getInt(p + 24) & 0xFFFFFFFFL;
            int nameLen = d.getShort(p + 28) & 0xFFFF, extraLen = d.getShort(p + 30) & 0xFFFF;
            int commentLen = d.getShort(p + 32) & 0xFFFF;
            long offset = d.getInt(p + 42) & 0xFFFFFFFFL;
            int flags = d.getShort(p + 8) & 0xFFFF;
            byte[] nb = new byte[nameLen];
            for (int i = 0; i < nameLen; i++) nb[i] = d.get(p + 46 + i);
            String name = new String(nb, (flags & 0x800) != 0 ? StandardCharsets.UTF_8 : StandardCharsets.ISO_8859_1);
            // ZIP64 extra field: the 64-bit values, in this order, for the 32-bit ones that are 0xFFFFFFFF.
            int e = p + 46 + nameLen, end = e + extraLen;
            while (e + 4 <= end) {
                int id = d.getShort(e) & 0xFFFF, len = d.getShort(e + 2) & 0xFFFF;
                if (id == 0x0001) {
                    int q = e + 4;
                    if (usize == 0xFFFFFFFFL && q + 8 <= e + 4 + len) { usize = d.getLong(q); q += 8; }
                    if (csize == 0xFFFFFFFFL && q + 8 <= e + 4 + len) { csize = d.getLong(q); q += 8; }
                    if (offset == 0xFFFFFFFFL && q + 8 <= e + 4 + len) { offset = d.getLong(q); }
                }
                e += 4 + len;
            }
            p += 46 + nameLen + extraLen + commentLen;
            if (name.endsWith("/")) continue;
            ZipEntry z = new ZipEntry(name);
            z.setMethod(method == 8 ? ZipEntry.DEFLATED : ZipEntry.STORED);
            z.setSize(usize);
            z.setCompressedSize(csize);
            entries.put(name, z);
            offsets.put(name, offset);
        }
    }

    /** The largest .iso in the zip, or null. */
    ZipEntry iso() {
        ZipEntry best = null;
        for (ZipEntry e : entries.values()) {
            if (e.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".iso")
                    && (best == null || e.getSize() > best.getSize())) best = e;
        }
        return best;
    }

    /** The STFS packages in the zip (game, DLC, title update), sorted by name. */
    List<ZipEntry> packages() {
        List<ZipEntry> out = new ArrayList<>();
        for (ZipEntry e : entries.values()) if (PACKAGE.matcher(e.getName()).find()) out.add(e);
        out.sort((a, b) -> a.getName().compareTo(b.getName()));
        return out;
    }

    /** A package's title ID folder (upper case), or "" when the zip holds the title folder's content. */
    static String titleId(ZipEntry e) {
        Matcher m = PACKAGE.matcher(e.getName());
        return m.find() && m.group(1) != null ? m.group(1).toUpperCase(java.util.Locale.ROOT) : "";
    }

    /** A package's content type folder ("000D0000" for the game). */
    static String contentType(ZipEntry e) {
        Matcher m = PACKAGE.matcher(e.getName());
        return m.find() ? m.group(2) : "";
    }

    ZipEntry entry(String name) { return entries.get(name); }

    // ---------------------------------------------------------------- an entry's data

    /** The first max bytes of an entry, inflated to out (headers, for a probe). */
    void inflateHead(ZipEntry e, File out, int max) throws IOException, Recipe.Failure {
        try (OutputStream o = new FileOutputStream(out)) {
            copy(e, o, max, (d, t) -> true);
        }
    }

    /**
     * Inflates an entry to out, through out.part, renamed when complete. A complete out (its
     * size the entry's) is kept, so an interrupted build doesn't inflate it again.
     */
    void inflate(ZipEntry e, File out, Progress progress) throws IOException, Recipe.Failure {
        if (out.isFile() && out.length() == e.getSize()) {
            if (!progress.onProgress(e.getSize(), e.getSize())) throw new Recipe.Failure("cancelled", "");
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        out.getParentFile().mkdirs();
        File part = new File(out.getPath() + ".part");
        boolean ok = false;
        try (OutputStream o = new FileOutputStream(part)) {
            long n = copy(e, o, Long.MAX_VALUE, progress);
            if (n != e.getSize()) throw new Recipe.Failure("extract_failed", e.getName() + ": " + n + " of " + e.getSize() + " bytes");
            ok = true;
        } finally {
            //noinspection ResultOfMethodCallIgnored
            if (!ok) part.delete();
        }
        //noinspection ResultOfMethodCallIgnored
        out.delete();
        if (!part.renameTo(out)) throw new IOException("can't rename " + part);
    }

    /** Writes up to max bytes of the entry's content to o; the count written. */
    private long copy(ZipEntry e, OutputStream o, long max, Progress progress) throws IOException, Recipe.Failure {
        Long offset = offsets.get(e.getName());
        if (offset == null) throw new Recipe.Failure("source_unreadable", "no " + e.getName() + " in the zip");
        ByteBuffer h = read(offset, 30);
        if (h.getInt(0) != LOCAL) throw new Recipe.Failure("extract_failed", e.getName() + ": damaged local header");
        long pos = offset + 30 + (h.getShort(26) & 0xFFFF) + (h.getShort(28) & 0xFFFF);
        long left = e.getCompressedSize(), total = e.getSize(), done = 0;
        ByteBuffer src = ByteBuffer.allocate(1 << 20);
        byte[] out = new byte[1 << 20];
        Inflater inflater = e.getMethod() == ZipEntry.DEFLATED ? new Inflater(true) : null;
        try {
            while (done < max && (left > 0 || (inflater != null && !inflater.finished()))) {
                if (inflater == null || inflater.needsInput()) {
                    if (left <= 0) {
                        if (inflater == null) break;
                        inflater.setInput(new byte[1]); // the raw deflate stream's dummy byte
                    } else {
                        src.clear().limit((int) Math.min(left, src.capacity()));
                        int n = channel.read(src, pos);
                        if (n <= 0) throw new Recipe.Failure("extract_failed", e.getName() + ": the zip is truncated");
                        pos += n;
                        left -= n;
                        src.flip();
                        if (inflater == null) {
                            int w = (int) Math.min(n, max - done);
                            o.write(src.array(), 0, w);
                            done += w;
                            if (!progress.onProgress(done, total)) throw new Recipe.Failure("cancelled", "");
                            continue;
                        }
                        inflater.setInput(src.array(), 0, n);
                    }
                }
                int n;
                while ((n = inflater.inflate(out, 0, (int) Math.min(out.length, max - done))) > 0) {
                    o.write(out, 0, n);
                    done += n;
                    if (done >= max) break;
                }
                if (!progress.onProgress(done, total)) throw new Recipe.Failure("cancelled", "");
                if (inflater.needsDictionary()) throw new Recipe.Failure("extract_failed", e.getName() + ": unsupported compression");
            }
        } catch (DataFormatException damaged) {
            throw new Recipe.Failure("extract_failed", e.getName() + ": " + damaged.getMessage());
        } finally {
            if (inflater != null) inflater.end();
        }
        return done;
    }

    private ByteBuffer read(long position, int size) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
        while (b.hasRemaining()) {
            if (channel.read(b, position + b.position()) <= 0) throw new IOException("end of the zip");
        }
        b.flip();
        return b;
    }

    @Override
    public void close() throws IOException {
        try {
            in.close();
        } finally {
            pfd.close();
        }
    }
}
