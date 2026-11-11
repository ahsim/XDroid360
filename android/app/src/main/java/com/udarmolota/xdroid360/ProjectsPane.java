package com.udarmolota.xdroid360;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * The Projects tab: the author's other projects (as in RimDroid and Zomdroid), from the list the
 * launchers share (projects.json in udarmolota/projects), carried in this APK's assets with the
 * projects' icons. XDroid360 has no network permission, so the list is the one of this release;
 * Download and Page open the browser.
 *
 * <p>projects.json: {"reddit": url, "projects": [{"name", "description" (a string, or an object by
 * language code with "en" as the fallback), "icon" (an https URL whose file name, without its
 * extension, is the built-in drawable), "github" ("owner/repo"), "url" (optional page)}]}.
 */
final class ProjectsPane {
    private static final String TAG = "XDroid360";
    private static final String ASSET = "projects/projects.json";
    /** This launcher's own repository: its card says "You are here". */
    private static final String OWN_REPO = "udarmolota/XDroid360";

    private ProjectsPane() {}

    static void fill(Activity a, View reddit, LinearLayout list) {
        list.removeAllViews();
        String redditUrl = "https://www.reddit.com/r/UdarMolotaDev";
        try (InputStream in = a.getAssets().open(ASSET)) {
            JSONObject root = new JSONObject(new String(readAll(in), StandardCharsets.UTF_8));
            String r = root.optString("reddit", "");
            if (r.startsWith("https://")) redditUrl = r;
            JSONArray projects = root.optJSONArray("projects");
            for (int i = 0; projects != null && i < projects.length(); i++) {
                JSONObject p = projects.optJSONObject(i);
                if (p != null) list.addView(card(a, list, p));
            }
        } catch (Exception e) {
            Log.w(TAG, "projects.json: " + e);
        }
        String url = redditUrl;
        reddit.setOnClickListener(v -> open(a, url));
    }

    private static View card(Activity a, LinearLayout list, JSONObject p) {
        View card = LayoutInflater.from(a).inflate(R.layout.item_project, list, false);
        String github = p.optString("github", "");
        String page = p.optString("url", github.isEmpty() ? "" : "https://github.com/" + github);
        ((TextView) card.findViewById(R.id.project_name)).setText(p.optString("name", ""));
        ((TextView) card.findViewById(R.id.project_description)).setText(description(a, p));
        card.findViewById(R.id.project_here).setVisibility(OWN_REPO.equalsIgnoreCase(github) ? View.VISIBLE : View.GONE);

        View download = card.findViewById(R.id.project_download);
        if (github.isEmpty()) download.setVisibility(View.GONE);
        else download.setOnClickListener(v -> open(a, "https://github.com/" + github + "/releases/latest"));
        View pageButton = card.findViewById(R.id.project_page);
        if (page.startsWith("https://")) pageButton.setOnClickListener(v -> open(a, page));
        else pageButton.setVisibility(View.GONE);

        // The icon built in under the link's file name (icons/project_valdroid.png -> project_valdroid).
        String icon = p.optString("icon", "");
        String name = icon.substring(icon.lastIndexOf('/') + 1);
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        int res = name.isEmpty() ? 0 : a.getResources().getIdentifier(name, "drawable", a.getPackageName());
        if (res != 0) ((ImageView) card.findViewById(R.id.project_icon)).setImageResource(res);
        return card;
    }

    /** The description in the launcher's language, English otherwise. */
    private static String description(Activity a, JSONObject p) {
        JSONObject byLang = p.optJSONObject("description");
        if (byLang == null) return p.optString("description", "");
        String lang = a.getResources().getConfiguration().getLocales().get(0).getLanguage();
        String text = byLang.optString(lang, "");
        if (text.isEmpty() && "in".equals(lang)) text = byLang.optString("id", "");
        return text.isEmpty() ? byLang.optString("en", "") : text;
    }

    private static void open(Activity a, String url) {
        try {
            a.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (Exception e) {
            Log.w(TAG, "cannot open " + url + ": " + e);
        }
    }

    private static byte[] readAll(InputStream in) throws java.io.IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }
}
