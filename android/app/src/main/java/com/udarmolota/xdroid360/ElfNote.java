package com.udarmolota.xdroid360;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * The ABI a game library was built for, from its "XDroid360" ELF note (xdroid360_note.c),
 * read from the file: loading the library into the launcher would run the game's static
 * initializers, and it could never be unloaded.
 */
final class ElfNote {
    private static final int PT_NOTE = 4;
    private static final int NOTE_TYPE_ABI = 1;
    private static final String NOTE_NAME = "XDroid360";

    private ElfNote() {}

    /** The library's ABI, or -1 when it has no such note (or isn't a 64-bit little-endian ELF). */
    static int abi(File library) throws IOException {
        try (RandomAccessFile f = new RandomAccessFile(library, "r")) {
            ByteBuffer h = read(f, 0, 64);
            if (h.getInt(0) != 0x464C457F || h.get(4) != 2 || h.get(5) != 1) return -1; // \x7fELF, 64-bit, LE
            long phoff = h.getLong(0x20);
            int phentsize = h.getShort(0x36) & 0xFFFF;
            int phnum = h.getShort(0x38) & 0xFFFF;
            if (phentsize < 0x38 || phnum > 256) return -1;
            for (int i = 0; i < phnum; i++) {
                ByteBuffer ph = read(f, phoff + (long) i * phentsize, 0x38);
                if (ph.getInt(0) != PT_NOTE) continue;
                long offset = ph.getLong(0x08), size = ph.getLong(0x20);
                if (size <= 0 || size > 1 << 20) continue;
                int abi = find(read(f, offset, (int) size));
                if (abi >= 0) return abi;
            }
            return -1;
        }
    }

    private static int find(ByteBuffer notes) {
        int p = 0, end = notes.limit();
        while (p + 12 <= end) {
            int namesz = notes.getInt(p), descsz = notes.getInt(p + 4), type = notes.getInt(p + 8);
            int name = p + 12, desc = name + align4(namesz), next = desc + align4(descsz);
            if (namesz < 0 || descsz < 0 || next > end || next <= p) return -1;
            if (type == NOTE_TYPE_ABI && namesz == NOTE_NAME.length() + 1 && descsz >= 4) {
                byte[] n = new byte[namesz - 1];
                for (int i = 0; i < n.length; i++) n[i] = notes.get(name + i);
                if (NOTE_NAME.equals(new String(n, StandardCharsets.US_ASCII))) return notes.getInt(desc);
            }
            p = next;
        }
        return -1;
    }

    private static int align4(int n) { return (n + 3) & ~3; }

    private static ByteBuffer read(RandomAccessFile f, long offset, int size) throws IOException {
        byte[] b = new byte[size];
        f.seek(offset);
        f.readFully(b);
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);
    }
}
