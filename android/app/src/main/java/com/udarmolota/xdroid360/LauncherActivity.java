package com.udarmolota.xdroid360;

import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.format.Formatter;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ImageView;
import android.widget.PopupMenu;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.navigation.NavigationView;
import com.google.android.material.progressindicator.LinearProgressIndicator;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;

/**
 * The launcher: the installed games (instances) as cards (the game's own splash from the
 * player's disc, Play, settings, more), "Add game" (the player's disc image or game folder,
 * built into a game on the phone: BuildActivity), the builds that haven't finished, and the
 * side menu (driver, saves, the pad's layout, the log, help).
 */
public class LauncherActivity extends AppCompatActivity {
    private final List<Instance> instances = new ArrayList<>();
    private final List<BuildJob> builds = new ArrayList<>();
    private final Map<String, Bitmap> banners = new HashMap<>();
    private RecyclerView.Adapter<Holder> adapter;
    private View empty;
    private DrawerLayout drawer;
    private final Handler ui = new Handler(Looper.getMainLooper());

    // The document pickers, with what to do with the pick (set just before launching).
    private BiConsumer<Instance, Uri> onPick;
    private Instance pickFor;
    // "Add game": a disc image, or a folder of packages (by the games' recipes, source.pick).
    private final ActivityResultLauncher<String[]> pickImage =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) build(uri, "file");
            });
    private final ActivityResultLauncher<Uri> pickTree =
            registerForActivityResult(new ActivityResultContracts.OpenDocumentTree(), uri -> {
                if (uri != null) build(uri, "tree");
            });
    private Instance exportFor;
    private final ActivityResultLauncher<String> exportSaves =
            registerForActivityResult(new ActivityResultContracts.CreateDocument("application/zip"), uri -> {
                if (uri != null && exportFor != null) InstanceActions.exportSaves(this, exportFor, uri);
            });
    private final ActivityResultLauncher<String> exportPad =
            registerForActivityResult(new ActivityResultContracts.CreateDocument("application/json"), uri -> {
                if (uri != null && exportFor != null) exportPadTo(exportFor, uri);
            });
    private final ActivityResultLauncher<String[]> pickFile =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null && onPick != null && pickFor != null) {
                    onPick.accept(pickFor, uri);
                    refresh();
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
        drawer = findViewById(R.id.drawer);
        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.inflateMenu(R.menu.menu_launcher);
        toolbar.setNavigationOnClickListener(v -> drawer.openDrawer(GravityCompat.START));
        toolbar.setOnMenuItemClickListener(this::onMenu);

        NavigationView nav = findViewById(R.id.nav);
        nav.setNavigationItemSelectedListener(item -> {
            drawer.closeDrawer(GravityCompat.START);
            return onMenu(item);
        });
        try {
            String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            ((TextView) nav.getHeaderView(0).findViewById(R.id.nav_version)).setText(version);
        } catch (Exception ignored) {
        }

        empty = findViewById(R.id.empty);
        findViewById(R.id.empty_add).setOnClickListener(v -> addGame());
        findViewById(R.id.empty_help).setOnClickListener(v -> showHow());

        RecyclerView list = findViewById(R.id.instances);
        list.setLayoutManager(new LinearLayoutManager(this));
        adapter = new RecyclerView.Adapter<Holder>() {
            @NonNull
            @Override
            public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
                return new Holder(LayoutInflater.from(parent.getContext()).inflate(R.layout.item_instance, parent, false));
            }

            @Override
            public void onBindViewHolder(@NonNull Holder h, int position) {
                // The builds that haven't finished first, then the games.
                if (position < builds.size()) bind(h, builds.get(position));
                else bind(h, instances.get(position - builds.size()));
            }

            @Override
            public int getItemCount() { return builds.size() + instances.size(); }
        };
        list.setAdapter(adapter);

        bottomNav = findViewById(R.id.bottom_nav);
        bottomNav.setOnItemSelectedListener(this::onBottomNav);
        getOnBackPressedDispatcher().addCallback(this, new androidx.activity.OnBackPressedCallback(true) {
            @Override
            public void handleOnBackPressed() {
                // Back from another tab goes home first; from home it leaves as usual.
                if (bottomNav.getSelectedItemId() != R.id.tab_home) {
                    bottomNav.setSelectedItemId(R.id.tab_home);
                } else {
                    setEnabled(false);
                    getOnBackPressedDispatcher().onBackPressed();
                    setEnabled(true);
                }
            }
        });
    }

    // -------------------- the bottom bar --------------------

    private com.google.android.material.bottomnavigation.BottomNavigationView bottomNav;
    private boolean projectsFilled;
    /** The Import/Export tab's mode and game. */
    private boolean transferExport;
    private Instance transferFor;

    private boolean onBottomNav(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.tab_controls) {
            // An action, not a tab: the editor opens and the current tab stays selected.
            InstanceActions.choose(this, R.string.nav_pad_editor, instances,
                    i -> startActivity(new Intent(this, PadEditorActivity.class).putExtra(InstanceActivity.EXTRA_ID, i.id)));
            return false;
        }
        View home = findViewById(R.id.home_pane), transfer = findViewById(R.id.transfer_pane),
                projects = findViewById(R.id.projects_pane);
        home.setVisibility(id == R.id.tab_home ? View.VISIBLE : View.GONE);
        transfer.setVisibility(id == R.id.tab_import || id == R.id.tab_export ? View.VISIBLE : View.GONE);
        projects.setVisibility(id == R.id.tab_projects ? View.VISIBLE : View.GONE);
        if (id == R.id.tab_import || id == R.id.tab_export) {
            transferExport = id == R.id.tab_export;
            bindTransfer();
        } else if (id == R.id.tab_projects && !projectsFilled) {
            ProjectsPane.fill(this, findViewById(R.id.projects_reddit), findViewById(R.id.projects_list));
            projectsFilled = true;
        }
        return true;
    }

    /** Import or Export: the game (one radio button each), then its saves and its pad layout. */
    private void bindTransfer() {
        boolean none = instances.isEmpty();
        findViewById(R.id.transfer_none).setVisibility(none ? View.VISIBLE : View.GONE);
        for (int card : new int[] { R.id.transfer_game_card, R.id.transfer_saves_card, R.id.transfer_pad_card }) {
            findViewById(card).setVisibility(none ? View.GONE : View.VISIBLE);
        }
        if (none) return;
        // The game picked before (refresh() makes new Instance objects: matched by id).
        Instance again = instances.get(0);
        for (Instance i : instances) if (transferFor != null && i.id.equals(transferFor.id)) again = i;
        transferFor = again;
        android.widget.RadioGroup group = findViewById(R.id.transfer_game);
        group.setOnCheckedChangeListener(null);
        group.removeAllViews();
        for (Instance i : instances) {
            com.google.android.material.radiobutton.MaterialRadioButton b =
                    new com.google.android.material.radiobutton.MaterialRadioButton(this);
            b.setId(View.generateViewId());
            b.setText(i.name);
            b.setTag(i);
            group.addView(b);
            if (i == transferFor) group.check(b.getId());
        }
        group.setOnCheckedChangeListener((g, checked) -> transferFor = (Instance) g.findViewById(checked).getTag());

        ((TextView) findViewById(R.id.transfer_saves_hint)).setText(
                transferExport ? R.string.transfer_saves_export_hint : R.string.transfer_saves_import_hint);
        ((TextView) findViewById(R.id.transfer_pad_hint)).setText(
                transferExport ? R.string.transfer_pad_export_hint : R.string.transfer_pad_import_hint);
        int button = transferExport ? R.string.transfer_export_button : R.string.transfer_import_button;
        ((TextView) findViewById(R.id.transfer_saves)).setText(button);
        ((TextView) findViewById(R.id.transfer_pad)).setText(button);
        findViewById(R.id.transfer_saves).setOnClickListener(v -> {
            if (transferExport) exportSavesOf(transferFor);
            else pick(transferFor, InstanceActions::installSaves);
        });
        findViewById(R.id.transfer_pad).setOnClickListener(v -> {
            if (transferExport) {
                exportFor = transferFor;
                exportPad.launch(padFileName(transferFor));
            } else {
                pick(transferFor, LauncherActivity::importPadFrom);
            }
        });
    }

    private static String padFileName(Instance i) {
        String game = Instance.DAO_TITLE_ID.equals(i.titleId()) ? "DAO"
                : Instance.SOD_TITLE_ID.equals(i.titleId()) ? "SoD" : i.titleId();
        return game + "-gamepad.json";
    }

    private void exportPadTo(Instance i, Uri uri) {
        try (java.io.OutputStream out = getContentResolver().openOutputStream(uri)) {
            if (out == null) throw new java.io.IOException("can't write " + uri);
            out.write(VirtualPadView.exportLayout(this, i.id).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            android.widget.Toast.makeText(this, R.string.pad_exported, android.widget.Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            android.widget.Toast.makeText(this, e.toString(), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    private static void importPadFrom(LauncherActivity a, Instance i, Uri uri) {
        String json;
        try (java.io.InputStream in = a.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new java.io.IOException("can't read " + uri);
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] b = new byte[8192];
            int n;
            while ((n = in.read(b)) > 0 && out.size() < (1 << 20)) out.write(b, 0, n);
            json = out.toString("UTF-8");
        } catch (java.io.IOException e) {
            android.widget.Toast.makeText(a, e.toString(), android.widget.Toast.LENGTH_LONG).show();
            return;
        }
        boolean ok = VirtualPadView.importLayout(a, i.id, json);
        android.widget.Toast.makeText(a, ok ? R.string.pad_imported : R.string.pad_import_bad,
                android.widget.Toast.LENGTH_LONG).show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        instances.clear();
        instances.addAll(Instance.list(this));
        builds.clear();
        builds.addAll(BuildJob.unfinished(this));
        adapter.notifyDataSetChanged();
        empty.setVisibility(instances.isEmpty() && builds.isEmpty() ? View.VISIBLE : View.GONE);
        if (findViewById(R.id.transfer_pane).getVisibility() == View.VISIBLE) bindTransfer();
    }

    /** A game being built (or whose build stopped): its build screen, or delete. */
    private void bind(Holder h, BuildJob job) {
        h.name.setText(job.name);
        h.details.setText(BuildService.isRunning(job.id) ? R.string.build_running : R.string.build_unfinished);
        h.banner.setImageBitmap(null);
        h.settings.setVisibility(View.GONE);
        h.play.setVisibility(View.GONE);
        View.OnClickListener open = v -> startActivity(new Intent(this, BuildActivity.class)
                .putExtra(BuildActivity.EXTRA_JOB, job.id));
        h.banner.setOnClickListener(open);
        h.itemView.setOnClickListener(open);
        h.more.setOnClickListener(open);
    }

    private void bind(Holder h, Instance i) {
        h.settings.setVisibility(View.VISIBLE);
        h.play.setVisibility(View.VISIBLE);
        h.itemView.setOnClickListener(null);
        h.name.setText(i.name);
        h.details.setText(i.buildLabel());
        Bitmap b = banners.get(i.id);
        h.banner.setImageBitmap(b);
        if (b == null) {
            new Thread(() -> {
                Bitmap made = Banner.load(this, i);
                if (made != null) ui.post(() -> {
                    banners.put(i.id, made);
                    int at = instances.indexOf(i);
                    if (at >= 0) adapter.notifyItemChanged(at);
                });
            }).start();
        }
        h.play.setOnClickListener(v -> play(i));
        h.banner.setOnClickListener(v -> play(i));
        h.settings.setOnClickListener(v -> startActivity(
                new Intent(this, InstanceActivity.class).putExtra(InstanceActivity.EXTRA_ID, i.id)));
        h.more.setOnClickListener(v -> {
            PopupMenu menu = new PopupMenu(this, v);
            menu.inflate(R.menu.menu_instance);
            menu.setOnMenuItemClickListener(item -> {
                int id = item.getItemId();
                if (id == R.id.instance_update) InstanceActions.update(this, i);
                else if (id == R.id.instance_delete) InstanceActions.confirmDelete(this, i, this::refresh);
                return true;
            });
            menu.show();
        });
    }

    private void play(Instance i) {
        startActivity(new Intent(this, GameActivity.class).putExtra(InstanceActivity.EXTRA_ID, i.id));
    }

    private boolean onMenu(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.action_add || id == R.id.nav_add) addGame();
        else if (id == R.id.action_help || id == R.id.nav_how) showHow();
        else if (id == R.id.nav_driver) InstanceActions.choose(this, R.string.nav_install_driver, instances,
                i -> pick(i, InstanceActions::installDriver));
        else if (id == R.id.nav_bug) bugReport(instances.isEmpty() ? null : instances.get(0));
        else if (id == R.id.nav_about) showAbout();
        else return false;
        return true;
    }

    private interface Install { void run(LauncherActivity a, Instance i, Uri uri); }

    private void pick(Instance i, Install install) {
        pickFor = i;
        onPick = (inst, uri) -> install.run(this, inst, uri);
        pickFile.launch(new String[] { "*/*" });
    }

    private void exportSavesOf(Instance i) {
        exportFor = i;
        exportSaves.launch(InstanceActions.savesFileName(i));
    }

    private void bugReport(Instance i) {
        Intent intent = new Intent(this, BugReportActivity.class);
        if (i != null) intent.putExtra(InstanceActivity.EXTRA_ID, i.id);
        startActivity(intent);
    }

    /** "Add game": the game's name, then its file. */
    private void addGame() {
        List<Recipe> games = Toolchain.profiles(this);
        if (games.isEmpty() || !Toolchain.present(this)) {
            new MaterialAlertDialogBuilder(this).setTitle(R.string.launcher_add).setMessage(R.string.build_no_toolchain)
                    .setPositiveButton(android.R.string.ok, null).show();
            return;
        }
        // Only the games' names: every game comes as a file (a disc image, or a .zip), and the
        // card after the pick says which game it is. (A folder pick, pickTree, is kept in code.)
        if (games.size() == 1) {
            pickImage.launch(new String[] { "*/*" });
            return;
        }
        String[] names = new String[games.size()];
        for (int i = 0; i < names.length; i++) names[i] = games.get(i).title();
        new MaterialAlertDialogBuilder(this).setTitle(R.string.launcher_add)
                .setItems(names, (d, which) -> pickImage.launch(new String[] { "*/*" }))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void build(Uri uri, String pick) {
        startActivity(new Intent(this, BuildActivity.class).putExtra(BuildActivity.EXTRA_URI, uri)
                .putExtra(BuildActivity.EXTRA_PICK, pick));
    }

    private void showHow() {
        new MaterialAlertDialogBuilder(this).setTitle(R.string.launcher_how).setMessage(R.string.launcher_how_text)
                .setPositiveButton(android.R.string.ok, null).show();
    }

    private void showAbout() {
        new MaterialAlertDialogBuilder(this).setTitle(R.string.app_name).setMessage(R.string.about_text)
                .setPositiveButton(android.R.string.ok, null)
                .setNegativeButton(R.string.about_licenses, (d, w) -> showLicenses())
                .setNeutralButton("GitHub", (d, w) -> startActivity(new Intent(Intent.ACTION_VIEW,
                        Uri.parse("https://github.com/udarmolota/XDroid360"))))
                .show();
    }

    /** The third-party components and their licences (NOTICE.md; the full texts are in the
     *  licenses/ folders of the kit and the game profiles the APK carries). */
    private void showLicenses() {
        new MaterialAlertDialogBuilder(this).setTitle(R.string.about_licenses)
                .setMessage(R.string.about_licenses_text)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final TextView name, details;
        final ImageView banner;
        final View play, settings, more;

        Holder(View v) {
            super(v);
            name = v.findViewById(R.id.name);
            details = v.findViewById(R.id.details);
            banner = v.findViewById(R.id.banner);
            play = v.findViewById(R.id.play);
            settings = v.findViewById(R.id.settings);
            more = v.findViewById(R.id.more);
        }
    }
}
