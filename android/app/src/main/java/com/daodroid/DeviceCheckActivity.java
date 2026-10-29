package com.daodroid;

import android.graphics.Color;
import android.os.Bundle;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;

import java.util.List;
import java.util.Map;

/**
 * "Device check" (side menu): the phone, then each Vulkan driver the game could run on with
 * DeviceCheck's verdict. Runs in its own process (:probe, AndroidManifest.xml): the checks load
 * the drivers into it, and a driver that crashes takes only this screen down; opened again, it
 * shows that driver as crashed instead of loading it again ("Check again" retries).
 */
public class DeviceCheckActivity extends AppCompatActivity {
    private ViewGroup results;
    private View again;
    private Thread worker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_device_check);
        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material);
        toolbar.setNavigationOnClickListener(v -> finish());
        ((TextView) findViewById(R.id.phone)).setText(DeviceCheck.phone(this));
        results = findViewById(R.id.results);
        again = findViewById(R.id.again);
        again.setOnClickListener(v -> check(true));
        check(false);
    }

    private void check(boolean retryCrashed) {
        if (worker != null && worker.isAlive()) return;
        results.removeAllViews();
        again.setEnabled(false);
        List<DeviceCheck.Target> targets = DeviceCheck.targets(this);
        View[] cards = new View[targets.size()];
        for (int i = 0; i < cards.length; i++) {
            cards[i] = LayoutInflater.from(this).inflate(R.layout.item_device_check, results, false);
            ((TextView) cards[i].findViewById(R.id.title)).setText(targets.get(i).label);
            ((TextView) cards[i].findViewById(R.id.verdict)).setText(R.string.check_running);
            cards[i].findViewById(R.id.details_toggle).setVisibility(View.GONE);
            results.addView(cards[i]);
        }
        worker = new Thread(() -> {
            for (int i = 0; i < cards.length; i++) {
                DeviceCheck.Target t = targets.get(i);
                if (retryCrashed) DeviceCheck.clearCrash(this, t);
                DeviceCheck.Result crashed = DeviceCheck.crashed(this, t);
                DeviceCheck.Result r = crashed != null ? crashed : DeviceCheck.run(this, t);
                View card = cards[i];
                runOnUiThread(() -> show(card, r));
            }
            runOnUiThread(() -> again.setEnabled(true));
        }, "Device check");
        worker.start();
    }

    private void show(View card, DeviceCheck.Result r) {
        Map<String, String> v = r.values;
        String device = v.containsKey("device.name")
                ? v.get("device.name") + " · Vulkan " + v.get("device.api")
                        + (v.containsKey("driver.name") ? " · " + v.get("driver.name") + " " + v.getOrDefault("driver.info", "") : "")
                : "";
        ((TextView) card.findViewById(R.id.device)).setText(device.trim());

        TextView verdict = card.findViewById(R.id.verdict);
        verdict.setText(verdictText(r.level));
        verdict.setTextColor(color(r.level));

        SpannableStringBuilder s = new SpannableStringBuilder();
        for (DeviceCheck.Finding f : r.findings) {
            if (s.length() > 0) s.append('\n');
            int start = s.length();
            s.append("● ");
            s.setSpan(new ForegroundColorSpan(color(f.level)), start, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            s.append(f.text);
        }
        ((TextView) card.findViewById(R.id.findings)).setText(s);

        StringBuilder raw = new StringBuilder();
        if (r.target.path != null) raw.append(r.target.path).append('\n');
        for (Map.Entry<String, String> e : v.entrySet()) raw.append(e.getKey()).append(" = ").append(e.getValue()).append('\n');
        TextView details = card.findViewById(R.id.details);
        details.setText(raw.toString().trim());
        View toggle = card.findViewById(R.id.details_toggle);
        toggle.setVisibility(v.isEmpty() ? View.GONE : View.VISIBLE);
        toggle.setOnClickListener(b -> details.setVisibility(details.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
    }

    private int verdictText(DeviceCheck.Level level) {
        switch (level) {
            case FAIL: return R.string.check_verdict_fail;
            case PROBLEM: return R.string.check_verdict_problem;
            case WORKAROUND: return R.string.check_verdict_workaround;
            default: return R.string.check_verdict_ok;
        }
    }

    private int color(DeviceCheck.Level level) {
        switch (level) {
            case FAIL: return getColor(R.color.dao_red);
            case PROBLEM: return Color.rgb(0xD8, 0x43, 0x15);
            case WORKAROUND: return Color.rgb(0xB2, 0x6A, 0x00);
            case INFO: return Color.rgb(0x75, 0x75, 0x75);
            default: return Color.rgb(0x2E, 0x7D, 0x32);
        }
    }
}
