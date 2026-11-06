package com.udarmolota.xdroid360;

import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.text.format.Formatter;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ImageView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.materialswitch.MaterialSwitch;

import java.io.File;
import java.io.IOException;

/** One game's settings: its details, Play, the driver and graphics switches, saves, the log, Delete. */
public class InstanceActivity extends AppCompatActivity {
    static final String EXTRA_ID = "instance";
    private static final String[] DRIVERS = { Instance.DRIVER_AUTO, Instance.DRIVER_BUILTIN, Instance.DRIVER_SYSTEM, Instance.DRIVER_CUSTOM };
    private static final String[] SYSMEM = { Instance.SYSMEM_AUTO, Instance.SYSMEM_ON, Instance.SYSMEM_OFF };

    private Instance instance;
    private Spinner driver;

    private final ActivityResultLauncher<String[]> pickDriver =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) {
                    InstanceActions.installDriver(this, instance, uri);
                    driver.setSelection(indexOf(DRIVERS, instance.driver), false);
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_instance);
        instance = Instance.open(this, getIntent().getStringExtra(EXTRA_ID));

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setTitle(R.string.instance_settings);
        toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material);
        toolbar.setNavigationOnClickListener(v -> finish());

        ImageView banner = findViewById(R.id.banner);
        new Thread(() -> {
            Bitmap b = Banner.load(this, instance);
            if (b != null) runOnUiThread(() -> banner.setImageBitmap(b));
        }).start();

        StringBuilder d = new StringBuilder(instance.buildLabel());
        if (!instance.isDev()) {
            d.append("\n").append(Formatter.formatFileSize(this, instance.manifest.optLong("game_bytes", 0)));
            d.append(", ").append(instance.manifest.optInt("game_files", 0)).append(" files");
            org.json.JSONObject b = instance.manifest.optJSONObject("build");
            if (b != null) d.append("\nDAORecomp ").append(shortCommit(b.optString("daorecomp"))).append(", ").append(b.optString("created"));
        }
        ((TextView) findViewById(R.id.name)).setText(instance.name);
        ((TextView) findViewById(R.id.details)).setText(d);

        driver = findViewById(R.id.driver);
        bindSpinner(driver, R.array.drivers, indexOf(DRIVERS, instance.driver), i -> instance.driver = DRIVERS[i]);
        findViewById(R.id.driver_pick).setOnClickListener(v -> pickDriver.launch(new String[] { "*/*" }));
        bindSpinner(findViewById(R.id.sysmem), R.array.sysmem, indexOf(SYSMEM, instance.sysmem), i -> instance.sysmem = SYSMEM[i]);
        bindOptions();
        android.widget.RadioGroup lod = findViewById(R.id.lod);
        int[] lodIds = { R.id.lod_full, R.id.lod_low, R.id.lod_ultra_low };
        lod.check(lodIds[Math.max(0, Math.min(2, instance.textureLodBias))]);
        lod.setOnCheckedChangeListener((g, checked) -> {
            for (int i = 0; i < lodIds.length; i++) if (lodIds[i] == checked) instance.textureLodBias = i;
            save();
        });
        View advanced = findViewById(R.id.advanced);
        View chevron = findViewById(R.id.advanced_chevron);
        findViewById(R.id.advanced_header).setOnClickListener(v -> {
            boolean open = advanced.getVisibility() != View.VISIBLE;
            advanced.setVisibility(open ? View.VISIBLE : View.GONE);
            chevron.setRotation(open ? 180 : 0);
        });
        android.widget.RadioGroup hud = findViewById(R.id.hud);
        int[] hudIds = { R.id.hud_off, R.id.hud_fps, R.id.hud_full };
        hud.check(hudIds[Math.max(0, Math.min(2, instance.hudMode))]);
        hud.setOnCheckedChangeListener((g, checked) -> {
            for (int i = 0; i < hudIds.length; i++) if (hudIds[i] == checked) instance.hudMode = i;
            save();
        });
        MaterialSwitch vibrate = findViewById(R.id.vibrate);
        vibrate.setChecked(instance.vibrateOnTouch);
        vibrate.setOnCheckedChangeListener((b, on) -> { instance.vibrateOnTouch = on; save(); });

        env = findViewById(R.id.env);
        env.setText(readEnv());
    }

    private com.google.android.material.textfield.TextInputEditText env;

    @Override
    protected void onPause() {
        super.onPause();
        writeEnv();
    }

    /** userdata/env.txt: KEY=VALUE lines the game starts with (GameActivity.applyEnvFile). */
    private File envFile() {
        return new File(instance.userdata(), "env.txt");
    }

    private String readEnv() {
        try {
            return envFile().isFile() ? new String(java.nio.file.Files.readAllBytes(envFile().toPath()),
                    java.nio.charset.StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private void writeEnv() {
        if (env == null || env.getText() == null) return;
        String text = env.getText().toString().replace("\r", "").trim();
        try {
            if (text.isEmpty()) {
                //noinspection ResultOfMethodCallIgnored
                envFile().delete();
            } else {
                //noinspection ResultOfMethodCallIgnored
                instance.userdata().mkdirs();
                java.nio.file.Files.write(envFile().toPath(), (text + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
        } catch (IOException e) {
            Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    /** The settings only some games have, offered by their package (manifest "options"). */
    private void bindOptions() {
        java.util.List<String> presets = instance.presets();
        if (!presets.isEmpty()) {
            java.util.List<String> ids = new java.util.ArrayList<>();
            java.util.List<CharSequence> labels = new java.util.ArrayList<>();
            ids.add("");
            labels.add(getString(R.string.preset_default));
            for (String id : presets) {
                ids.add(id);
                labels.add(presetLabel(id));
            }
            int at = Math.max(0, ids.indexOf(instance.preset));
            showSpinner(R.id.preset_label, R.id.preset, labels, at, i -> instance.preset = ids.get(i));
        }
        org.json.JSONObject frameRate = instance.option("frame_rate");
        org.json.JSONArray choices = frameRate != null ? frameRate.optJSONArray("choices") : null;
        if (choices != null && choices.length() > 0) {
            java.util.List<Integer> rates = new java.util.ArrayList<>();
            java.util.List<CharSequence> labels = new java.util.ArrayList<>();
            for (int i = 0; i < choices.length(); i++) {
                rates.add(choices.optInt(i));
                labels.add(getString(R.string.frame_rate_value, choices.optInt(i)));
            }
            int at = Math.max(0, rates.indexOf(instance.frameRateToUse()));
            showSpinner(R.id.frame_rate_label, R.id.frame_rate, labels, at, i -> instance.frameRate = rates.get(i));
        }
    }

    /** A preset's name in the player's language for the common ids, else the id itself. */
    private CharSequence presetLabel(String id) {
        switch (id) {
            case "medium": return getString(R.string.preset_medium);
            case "balanced": return getString(R.string.preset_balanced);
            case "low": return getString(R.string.preset_low);
            case "very_low": return getString(R.string.preset_very_low);
            default: return id;
        }
    }

    private void showSpinner(int labelId, int spinnerId, java.util.List<CharSequence> labels, int selected, OnPick onPick) {
        findViewById(labelId).setVisibility(View.VISIBLE);
        Spinner spinner = findViewById(spinnerId);
        spinner.setVisibility(View.VISIBLE);
        ArrayAdapter<CharSequence> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        listen(spinner, a, selected, onPick);
    }

    private interface OnPick { void picked(int index); }

    private void bindSpinner(Spinner spinner, int array, int selected, OnPick onPick) {
        ArrayAdapter<CharSequence> a = ArrayAdapter.createFromResource(this, array, android.R.layout.simple_spinner_item);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        listen(spinner, a, selected, onPick);
    }

    private void listen(Spinner spinner, ArrayAdapter<CharSequence> a, int selected, OnPick onPick) {
        spinner.setAdapter(a);
        spinner.setSelection(Math.max(0, selected), false);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                onPick.picked(position);
                save();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void save() {
        try {
            instance.save();
        } catch (IOException e) {
            Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    private static int indexOf(String[] values, String v) {
        for (int i = 0; i < values.length; i++) if (values[i].equals(v)) return i;
        return 0;
    }

    private static String shortCommit(String c) {
        return c != null && c.length() > 8 ? c.substring(0, 8) : c;
    }
}
