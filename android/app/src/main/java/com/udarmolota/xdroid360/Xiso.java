package com.udarmolota.xdroid360;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * An Xbox 360 disc image (XDVDFS), read through a seekable channel: the port of the PC tool's
 * reader (recomp/scripts/xiso.py). The image is the player's file, so its directory tables are
 * read with limits and its names are checked before they become paths.
 */
final class Xiso {
    private static final int SECTOR = 2048;
    private static final byte[] MAGIC = "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
    // Known game-partition offsets: plain XDVDFS, XGD1, XGD2, XGD3.
    private static final long[] PARTITION_OFFSETS = { 0x0L, 0x18300000L, 0xFD90000L, 0x2080000L };
    private static final int MAX_TABLE_SIZE = 16 << 20;
    private static final int MAX_DEPTH = 32;
    /** Disc content the game never reads (recomp/installer/pipeline.py SKIP_PREFIXES). */
    private static final String[] SKIP_PREFIXES = { "$SystemUpdate/" };

    static final class Entry {
        final String path;
        final boolean dir;
        final long sector, size;

        Entry(String path, boolean dir, long sector, long size) {
            this.path = path;
            this.dir = dir;
            this.sector = sector;
            this.size = size;
        }
    }

    interface Progress {
        /** false cancels. */
        boolean onProgress(long done, long total);
    }

    private final FileChannel channel;
    private final long base;
    final List<Entry> entries;

    /** Reads the image's folders; throws Failure for anything that isn't a readable XDVDFS image. */
    Xiso(FileChannel channel) throws Recipe.Failure {
        this.channel = channel;
        try {
            channel.position(0); // a stream (a cloud provider) can't seek
        } catch (IOException e) {
            throw new Recipe.Failure("source_not_seekable", e.toString());
        }
        try {
            base = findPartition();
            ByteBuffer root = read(base + 32L * SECTOR + 20, 8);
            entries = new ArrayList<>();
            walk(root.getInt(0) & 0xFFFFFFFFL, root.getInt(4) & 0xFFFFFFFFL, "", 0, new HashSet<>());
        } catch (IOException e) {
            throw new Recipe.Failure("source_unreadable", e.toString());
        }
    }

    private long findPartition() throws IOException, Recipe.Failure {
        for (long off : PARTITION_OFFSETS) {
            ByteBuffer b;
            try {
                b = read(off + 32L * SECTOR, MAGIC.length);
            } catch (IOException e) {
                continue; // past the end of a small file
            }
            boolean match = true;
            for (int i = 0; i < MAGIC.length; i++) match &= b.get(i) == MAGIC[i];
            if (match) return off;
        }
        throw new Recipe.Failure("unknown_title", "not an Xbox 360 disc image (no XDVDFS header)");
    }

    private void walk(long sector, long size, String prefix, int depth, Set<Long> tables)
            throws IOException, Recipe.Failure {
        // Each table once: a folder pointing back at its parent would loop.
        if (depth > MAX_DEPTH || size > MAX_TABLE_SIZE || !tables.add(sector)) {
            throw new Recipe.Failure("extract_failed", "the disc image's folders are damaged");
        }
        ByteBuffer table = read(base + sector * SECTOR, (int) size);
        Set<Integer> seen = new HashSet<>();
        ArrayDeque<Integer> stack = new ArrayDeque<>();
        stack.push(0);
        while (!stack.isEmpty()) {
            int off = stack.pop();
            if (!seen.add(off) || off + 14 > table.limit()) continue;
            int left = table.getShort(off) & 0xFFFF, right = table.getShort(off + 2) & 0xFFFF;
            long sec = table.getInt(off + 4) & 0xFFFFFFFFL, sz = table.getInt(off + 8) & 0xFFFFFFFFL;
            int attr = table.get(off + 12) & 0xFF, nlen = table.get(off + 13) & 0xFF;
            if (left == 0xFFFF && right == 0xFFFF && sec == 0xFFFFFFFFL) continue;
            if (off + 14 + nlen > table.limit()) throw new Recipe.Failure("extract_failed", "a damaged folder entry");
            byte[] n = new byte[nlen];
            for (int i = 0; i < nlen; i++) n[i] = table.get(off + 14 + i);
            String name = new String(n, StandardCharsets.ISO_8859_1);
            checkName(name);
            String path = prefix + name;
            boolean dir = (attr & 0x10) != 0;
            entries.add(new Entry(path, dir, sec, sz));
            if (dir && sz != 0) walk(sec, sz, path + "/", depth + 1, tables);
            if (left != 0 && left != 0xFFFF) stack.push(left * 4);
            if (right != 0 && right != 0xFFFF) stack.push(right * 4);
        }
    }

    private static void checkName(String name) throws Recipe.Failure {
        boolean bad = name.isEmpty() || name.equals(".") || name.equals("..");
        for (int i = 0; i < name.length() && !bad; i++) {
            char c = name.charAt(i);
            bad = c < 32 || c == '/' || c == '\\';
        }
        if (bad) throw new Recipe.Failure("extract_failed", "a file name the phone can't hold: " + name);
    }

    Entry find(String path) {
        for (Entry e : entries) if (!e.dir && e.path.equalsIgnoreCase(path)) return e;
        return null;
    }

    /** The first bytes of a file in the image (headers). */
    ByteBuffer head(Entry e, int max) throws IOException {
        return read(base + e.sector * SECTOR, (int) Math.min(e.size, max));
    }

    String sha256(Entry e) throws IOException {
        MessageDigest sha;
        try {
            sha = MessageDigest.getInstance("SHA-256");
        } catch (java.security.NoSuchAlgorithmException x) {
            throw new IllegalStateException(x);
        }
        ByteBuffer b = ByteBuffer.allocate(1 << 20);
        long pos = base + e.sector * SECTOR, left = e.size;
        while (left > 0) {
            b.clear().limit((int) Math.min(left, b.capacity()));
            int n = channel.read(b, pos);
            if (n <= 0) throw new IOException("the disc image is truncated");
            b.flip();
            sha.update(b);
            pos += n;
            left -= n;
        }
        StringBuilder s = new StringBuilder();
        for (byte x : sha.digest()) s.append(String.format("%02x", x));
        return s.toString();
    }

    /** The bytes extract() writes: every file the game reads. */
    long gameBytes() {
        long total = 0;
        for (Entry e : entries) if (!e.dir && !skipped(e)) total += e.size;
        return total;
    }

    private static boolean skipped(Entry e) {
        for (String p : SKIP_PREFIXES) if (e.path.startsWith(p)) return true;
        return false;
    }

    /**
     * Extracts the game's files under out. A file already there at its full size is kept, so
     * an interrupted extraction resumes.
     */
    void extract(File out, Progress progress) throws IOException, Recipe.Failure {
        String root = out.getCanonicalPath();
        long total = gameBytes(), done = 0;
        ByteBuffer b = ByteBuffer.allocate(1 << 20);
        for (Entry e : entries) {
            if (e.dir || skipped(e)) continue;
            File dst = new File(out, e.path);
            if (!dst.getCanonicalPath().startsWith(root + File.separator)) {
                throw new Recipe.Failure("extract_failed", "a file outside the image's folders: " + e.path);
            }
            if (!(dst.isFile() && dst.length() == e.size)) {
                //noinspection ResultOfMethodCallIgnored
                dst.getParentFile().mkdirs();
                File part = new File(dst.getPath() + ".part");
                try (FileOutputStream os = new FileOutputStream(part); FileChannel o = os.getChannel()) {
                    long pos = base + e.sector * SECTOR, left = e.size;
                    while (left > 0) {
                        b.clear().limit((int) Math.min(left, b.capacity()));
                        int n = channel.read(b, pos);
                        if (n <= 0) throw new Recipe.Failure("extract_failed", "the disc image is truncated at " + e.path);
                        b.flip();
                        while (b.hasRemaining()) o.write(b);
                        pos += n;
                        left -= n;
                        if (!progress.onProgress(done + e.size - left, total)) throw new Recipe.Failure("cancelled", "");
                    }
                }
                if (!part.renameTo(dst)) throw new IOException("can't rename " + part);
            }
            done += e.size;
            if (!progress.onProgress(done, total)) throw new Recipe.Failure("cancelled", "");
        }
    }

    /** The title ID in a default.xex's header (XEX2 execution info), upper case, or null. */
    static String xexTitleId(ByteBuffer xex) {
        xex.order(ByteOrder.BIG_ENDIAN);
        if (xex.limit() < 24 || xex.getInt(0) != 0x58455832) return null; // "XEX2"
        int count = xex.getInt(0x14);
        for (int i = 0; i < count && 24 + i * 8 + 8 <= xex.limit(); i++) {
            int key = xex.getInt(24 + i * 8), value = xex.getInt(24 + i * 8 + 4);
            if (key == 0x00040006 && value > 0 && value + 16 <= xex.limit()) {
                return String.format("%08X", xex.getInt(value + 12));
            }
        }
        return null;
    }

    private ByteBuffer read(long position, int size) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
        while (b.hasRemaining()) {
            if (channel.read(b, position + b.position()) <= 0) throw new IOException("end of the image");
        }
        b.flip();
        return b;
    }
}
