package com.udarmolota.xdroid360;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.channels.FileChannel;
import java.util.ArrayList;
import java.util.List;

/**
 * What the player picked (a disc image, a folder of STFS packages, or either in a .zip) and which game it is:
 * the title ID from its headers, matched to a profile's recipe. Only headers are read, plus
 * default.xex's hash, so the card shows up in seconds.
 */
final class GameSource {
    /** One STFS package: a file in a picked folder (uri), or an entry of a picked zip (entry). */
    static final class Package {
        final Uri uri;
        final String entry;
        final String contentType;
        final long size;

        Package(Uri uri, String entry, String contentType, long size) {
            this.uri = uri;
            this.entry = entry;
            this.contentType = contentType;
            this.size = size;
        }

        String key() { return entry != null ? entry : uri.toString(); }
    }

    final Uri uri;
    final String pick; // "file" or "tree"
    Recipe recipe;
    String titleId;
    String format;           // "xiso" or "stfs"
    long gameBytes;
    /** The supported version of default.xex (by the recipe's inputs), when it could be checked. */
    Boolean supportedVersion;
    final List<Package> packages = new ArrayList<>();
    /** Inside a zip: its disc image entry (DAO), else null (packages carry their own entries). */
    String zipIso;
    boolean zipped;
    /** The most the build's scratch copy of zipped content needs at once (on the game's volume). */
    long scratchBytes;

    private GameSource(Uri uri, String pick) {
        this.uri = uri;
        this.pick = pick;
    }

    /** Reads the pick and finds its profile; a Failure says why it can't be built. */
    static GameSource detect(Context context, Uri uri, String pick) throws Recipe.Failure {
        GameSource s = new GameSource(uri, pick);
        if ("tree".equals(pick)) s.detectTree(context);
        else s.detectFile(context);
        return s;
    }

    private void detectFile(Context context) throws Recipe.Failure {
        if (ZipSource.looksLikeZip(context, uri)) {
            detectZip(context);
            return;
        }
        try (ParcelFileDescriptor pfd = open(context, uri);
             FileInputStream in = new FileInputStream(pfd.getFileDescriptor());
             FileChannel channel = in.getChannel()) {
            Xiso iso;
            try {
                iso = new Xiso(channel);
            } catch (Recipe.Failure notIso) {
                if (!"unknown_title".equals(notIso.code)) throw notIso;
                detectStfsFile(pfd);
                return;
            }
            Xiso.Entry xex = iso.find("default.xex");
            if (xex == null) throw new Recipe.Failure("unknown_title", "the disc image has no default.xex");
            titleId = Xiso.xexTitleId(iso.head(xex, 1 << 16));
            format = "xiso";
            recipe = profileFor(context, titleId, format);
            gameBytes = iso.gameBytes();
            JSONArray accepted = xexHashes(recipe);
            if (accepted != null) {
                String sha = iso.sha256(xex);
                boolean ok = false;
                for (int i = 0; i < accepted.length(); i++) ok |= accepted.optString(i).equalsIgnoreCase(sha);
                supportedVersion = ok;
            }
        } catch (IOException e) {
            throw new Recipe.Failure("source_unreadable", e.toString());
        }
    }

    /**
     * A .zip: DAO's disc image in it, or SoD's console folder. Nothing big is inflated here: the
     * disc image is checked when the build has inflated it (before extracting from it), a package
     * by its header only. Sizes come from the zip (a probe of a header gives none).
     */
    private void detectZip(Context context) throws Recipe.Failure {
        zipped = true;
        try (ZipSource zip = ZipSource.open(context, uri)) {
            java.util.zip.ZipEntry iso = zip.iso();
            List<java.util.zip.ZipEntry> stfs = zip.packages();
            if (iso != null && stfs.isEmpty()) {
                // The only profile that reads disc images is the game's: its title, checked by
                // the recipe's hash of default.xex once the image is inflated.
                format = "xiso";
                for (Recipe r : Toolchain.profiles(context)) {
                    if (r.sourceFormats().contains("xiso") && !r.titleIds().isEmpty()) {
                        recipe = r;
                        titleId = r.titleIds().get(0);
                        break;
                    }
                }
                if (recipe == null) throw new Recipe.Failure("unknown_title", "no profile reads disc images");
                zipIso = iso.getName();
                gameBytes = iso.getSize();
                scratchBytes = iso.getSize();
                return;
            }
            if (stfs.isEmpty()) throw new Recipe.Failure("unknown_title", "the zip holds no disc image and no console folder");
            java.util.zip.ZipEntry game = null;
            for (java.util.zip.ZipEntry e : stfs) {
                if ("000D0000".equals(ZipSource.contentType(e))) {
                    game = e;
                    break;
                }
            }
            if (game == null) throw new Recipe.Failure("missing_content", "no game package (000D0000) in the zip");
            titleId = ZipSource.titleId(game);
            if (StfsExtractor.available()) {
                // Its header confirms it's the title's game package (the folder names could lie).
                File head = new File(context.getCacheDir(), "probe.stfs");
                try {
                    zip.inflateHead(game, head, 128 * 1024);
                    try (ParcelFileDescriptor p = ParcelFileDescriptor.open(head, ParcelFileDescriptor.MODE_READ_ONLY)) {
                        String probe = StfsExtractor.nativeProbe(p.getFd());
                        if (probe == null) throw new Recipe.Failure("extract_failed", game.getName() + " isn't an STFS package");
                        String id = new JSONObject(probe).optString("title_id").toUpperCase(java.util.Locale.ROOT);
                        if (!id.isEmpty()) titleId = id;
                    }
                } catch (JSONException e) {
                    throw new Recipe.Failure("extract_failed", e.toString());
                } finally {
                    //noinspection ResultOfMethodCallIgnored
                    head.delete();
                }
            }
            if (titleId.isEmpty()) throw new Recipe.Failure("unknown_title", "the zip's packages name no title");
            for (java.util.zip.ZipEntry e : stfs) {
                String folder = ZipSource.titleId(e);
                if (!folder.isEmpty() && !titleId.equals(folder)) continue; // another game's folder in the same zip
                packages.add(new Package(null, e.getName(), "0x" + ZipSource.contentType(e), e.getSize()));
                gameBytes += e.getSize();
                scratchBytes = Math.max(scratchBytes, e.getSize()); // one package at a time
            }
            format = "stfs";
            recipe = profileFor(context, titleId, format);
        } catch (IOException e) {
            throw new Recipe.Failure("source_unreadable", e.toString());
        }
    }

    private void detectStfsFile(ParcelFileDescriptor pfd) throws Recipe.Failure {
        throw new Recipe.Failure("unknown_title", "not an Xbox 360 disc image"
                + (StfsExtractor.available() && StfsExtractor.nativeProbe(pfd.getFd()) != null
                   ? " (an STFS package: pick its folder instead)" : ""));
    }

    private void detectTree(Context context) throws Recipe.Failure {
        if (!StfsExtractor.available()) throw new Recipe.Failure("unknown_title", "this launcher has no STFS reader");
        List<Uri> files = new ArrayList<>();
        Uri root = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));
        list(context.getContentResolver(), root, 0, files);
        String gameType = null;
        for (Uri f : files) {
            try (ParcelFileDescriptor pfd = open(context, f)) {
                String probe = StfsExtractor.nativeProbe(pfd.getFd());
                if (probe == null) continue;
                JSONObject h = new JSONObject(probe);
                String id = h.optString("title_id").toUpperCase(java.util.Locale.ROOT);
                if (titleId == null) titleId = id;
                if (!titleId.equals(id)) continue; // another game's package in the same folder
                packages.add(new Package(f, null, h.optString("content_type"), h.optLong("size")));
                gameBytes += h.optLong("size");
                if ("0x000D0000".equalsIgnoreCase(h.optString("content_type"))) gameType = h.optString("content_type");
            } catch (IOException | JSONException e) {
                android.util.Log.w("XDroid360", "probe " + f + ": " + e);
            }
        }
        if (titleId == null) throw new Recipe.Failure("unknown_title", "no Xbox 360 packages in the folder");
        format = "stfs";
        recipe = profileFor(context, titleId, format);
        if (gameType == null) throw new Recipe.Failure("missing_content", "no game package (000D0000) for " + titleId);
    }

    /** Files under a picked folder, a few levels down (the title folder, or the one above it). */
    private static void list(ContentResolver resolver, Uri dir, int depth, List<Uri> out) {
        if (depth > 3) return;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(dir, DocumentsContract.getDocumentId(dir));
        String[] cols = { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = resolver.query(children, cols, null, null, null)) {
            while (c != null && c.moveToNext()) {
                Uri child = DocumentsContract.buildDocumentUriUsingTree(dir, c.getString(0));
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(1))) list(resolver, child, depth + 1, out);
                else out.add(child);
            }
        }
    }

    private static Recipe profileFor(Context context, String titleId, String format) throws Recipe.Failure {
        if (titleId != null) {
            for (Recipe r : Toolchain.profiles(context)) {
                if (r.titleIds().contains(titleId) && r.sourceFormats().contains(format)) return r;
            }
        }
        throw new Recipe.Failure("unknown_title", "no profile for title " + titleId + " (" + format + ")");
    }

    /** The recipe's accepted default.xex hashes (inputs.xex.sha256), or null. */
    static JSONArray xexHashes(Recipe recipe) {
        JSONObject inputs = recipe.json.optJSONObject("inputs");
        JSONObject xex = inputs != null ? inputs.optJSONObject("xex") : null;
        return xex != null ? xex.optJSONArray("sha256") : null;
    }

    static ParcelFileDescriptor open(Context context, Uri uri) throws Recipe.Failure {
        try {
            ParcelFileDescriptor pfd = context.getContentResolver().openFileDescriptor(uri, "r");
            if (pfd == null) throw new Recipe.Failure("source_unreadable", "no file for " + uri);
            return pfd;
        } catch (IOException | SecurityException e) {
            throw new Recipe.Failure("source_unreadable", e.toString());
        }
    }
}
