package com.udarmolota.xdroid360;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A game profile's recipe.json (SD1/toolchain/RECIPE_FORMAT_DRAFT.md, format 2): how the phone
 * builds that game's libgame.so from the player's disc. The launcher knows nothing about a game
 * beyond what its recipe says; this class reads it and substitutes its variables.
 */
final class Recipe {
    static final int FORMAT = 2;

    /** The failure codes a recipe may use, each with a text in strings.xml (build_error_<code>). */
    static final String[] CODES = {
        "source_unreadable", "source_not_seekable", "unknown_title", "missing_content",
        "unsupported_game_version", "not_enough_space", "extract_failed", "tool_failed",
        "compile_failed", "profile_mismatch", "link_failed", "abi_mismatch", "cancelled",
    };

    /** A build failure: one of CODES, plus what happened for the log. */
    static final class Failure extends Exception {
        final String code;

        Failure(String code, String detail) {
            super(detail);
            this.code = isCode(code) ? code : "tool_failed";
        }
    }

    static boolean isCode(String code) {
        for (String c : CODES) if (c.equals(code)) return true;
        return false;
    }

    final JSONObject json;
    final String profileId; // the asset's name: games/<profileId>.zip

    Recipe(String profileId, JSONObject json) throws Failure {
        this.profileId = profileId;
        this.json = json;
        if (json.optInt("format", -1) != FORMAT) {
            throw new Failure("profile_mismatch", profileId + ": recipe format " + json.optInt("format", -1));
        }
        if (json.optJSONArray("steps") == null) throw new Failure("profile_mismatch", profileId + ": no steps");
    }

    JSONObject game() { return json.optJSONObject("game") != null ? json.optJSONObject("game") : new JSONObject(); }
    String gameId() { return game().optString("id", profileId); }
    String title() { return game().optString("title", gameId()); }
    String version() { return json.optString("version"); }
    int abi() { return requires().optInt("abi", -1); }
    int kit() { return requires().optInt("kit", -1); }
    JSONObject tools() { return requires().optJSONObject("tools") != null ? requires().optJSONObject("tools") : new JSONObject(); }
    private JSONObject requires() { return json.optJSONObject("requires") != null ? json.optJSONObject("requires") : new JSONObject(); }

    List<String> titleIds() {
        List<String> out = new ArrayList<>();
        JSONArray a = game().optJSONArray("title_ids");
        if (a != null) for (int i = 0; i < a.length(); i++) out.add(a.optString(i).toUpperCase(java.util.Locale.ROOT));
        return out;
    }

    List<String> sourceFormats() {
        List<String> out = new ArrayList<>();
        JSONObject s = json.optJSONObject("source");
        JSONArray a = s != null ? s.optJSONArray("formats") : null;
        if (a != null) for (int i = 0; i < a.length(); i++) out.add(a.optString(i));
        return out;
    }

    /** "file" (one image) or "tree" (a folder of packages). */
    String pick() {
        JSONObject s = json.optJSONObject("source");
        return s != null && "tree".equals(s.optString("pick")) ? "tree" : "file";
    }

    long spaceMb(String key) {
        JSONObject s = json.optJSONObject("space");
        return s != null ? s.optLong(key, 0) : 0;
    }

    /** {format, game.id, version, kit, tools}: a change of any after a launcher update offers Rebuild. */
    JSONObject stamp() {
        JSONObject o = new JSONObject();
        try {
            o.put("format", FORMAT);
            o.put("game", gameId());
            o.put("version", version());
            o.put("kit", kit());
            o.put("tools", tools());
        } catch (JSONException e) {
            throw new IllegalStateException(e);
        }
        return o;
    }

    // ---------------------------------------------------------------- variables

    private static final Pattern VAR = Pattern.compile("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}");

    /**
     * The variables of one build: the launcher's (TOOLS, KIT, ...) and the recipe's own "vars",
     * whose values may use the launcher's. A value is a String or a List of Strings.
     */
    static final class Vars {
        private final Map<String, Object> values = new HashMap<>();

        void put(String name, String value) { values.put(name, value); }

        void putAll(JSONObject vars) {
            if (vars == null) return;
            for (Iterator<String> it = vars.keys(); it.hasNext(); ) {
                String k = it.next();
                Object v = vars.opt(k);
                if (v instanceof JSONArray) {
                    JSONArray a = (JSONArray) v;
                    List<String> list = new ArrayList<>();
                    for (int i = 0; i < a.length(); i++) list.add(a.optString(i));
                    values.put(k, list);
                } else if (v != null) {
                    values.put(k, String.valueOf(v));
                }
            }
        }

        /** s with every ${NAME} replaced; an unknown name or a list in text is the recipe's error. */
        String text(String s) throws Failure {
            return text(s, null);
        }

        String text(String s, Map<String, String> extra) throws Failure {
            return text(s, extra, 0);
        }

        private String text(String s, Map<String, String> extra, int depth) throws Failure {
            if (depth > 8) throw new Failure("tool_failed", "recipe: variables refer to each other in a loop: " + s);
            StringBuilder out = new StringBuilder();
            Matcher m = VAR.matcher(s);
            int last = 0;
            while (m.find()) {
                out.append(s, last, m.start());
                String name = m.group(1);
                Object v = extra != null && extra.containsKey(name) ? extra.get(name) : values.get(name);
                if (!(v instanceof String)) {
                    throw new Failure("tool_failed", "recipe: ${" + name + "} is " + (v == null ? "not set" : "a list"));
                }
                // A recipe variable may itself use the launcher's (vars come before their use).
                out.append(extra == null || !extra.containsKey(name) ? text((String) v, extra, depth + 1) : v);
                last = m.end();
            }
            return out.append(s.substring(last)).toString();
        }

        /**
         * The argv of a step: "@NAME" expands a list variable, "@objects:<step>" a step's
         * objects (from objects), anything else is text.
         */
        List<String> args(JSONArray args, Map<String, String> extra, Map<String, List<String>> objects) throws Failure {
            List<String> out = new ArrayList<>();
            if (args == null) return out;
            for (int i = 0; i < args.length(); i++) {
                String a = args.optString(i);
                if (a.startsWith("@objects:")) {
                    List<String> o = objects.get(a.substring("@objects:".length()));
                    if (o == null) throw new Failure("tool_failed", "recipe: " + a + " before that step ran");
                    out.addAll(o);
                } else if (a.startsWith("@") && values.get(a.substring(1)) instanceof List) {
                    for (Object item : (List<?>) values.get(a.substring(1))) out.add(text((String) item, extra));
                } else if (a.startsWith("@")) {
                    throw new Failure("tool_failed", "recipe: " + a + " is not a list variable");
                } else {
                    out.add(text(a, extra));
                }
            }
            return out;
        }
    }
}
