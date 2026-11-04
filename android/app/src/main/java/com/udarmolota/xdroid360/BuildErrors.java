package com.udarmolota.xdroid360;

import android.content.Context;

/** The player's text for each of the recipe format's failure codes (Recipe.CODES). */
final class BuildErrors {
    private BuildErrors() {}

    static String text(Context context, String code) {
        int id;
        switch (code == null ? "" : code) {
            case "source_unreadable": id = R.string.build_error_source_unreadable; break;
            case "source_not_seekable": id = R.string.build_error_source_not_seekable; break;
            case "unknown_title": id = R.string.build_error_unknown_title; break;
            case "missing_content": id = R.string.build_error_missing_content; break;
            case "unsupported_game_version": id = R.string.build_error_unsupported_game_version; break;
            case "not_enough_space": id = R.string.build_error_not_enough_space; break;
            case "extract_failed": id = R.string.build_error_extract_failed; break;
            case "compile_failed": id = R.string.build_error_compile_failed; break;
            case "profile_mismatch": id = R.string.build_error_profile_mismatch; break;
            case "link_failed": id = R.string.build_error_link_failed; break;
            case "abi_mismatch": id = R.string.build_error_abi_mismatch; break;
            case "cancelled": id = R.string.build_error_cancelled; break;
            default: id = R.string.build_error_tool_failed; break;
        }
        return context.getString(id);
    }
}
