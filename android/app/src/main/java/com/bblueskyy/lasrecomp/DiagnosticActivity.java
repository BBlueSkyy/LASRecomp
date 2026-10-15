package com.bblueskyy.lasrecomp;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Independent synthetic Vulkan/host checks, never a substitute for game execution. */
public final class DiagnosticActivity extends Activity {
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView report;
    private SurfaceView surface;
    private boolean loaded;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 24, 24, 24);
        root.setBackgroundColor(Color.rgb(19, 23, 29));
        TextView heading = new TextView(this);
        heading.setText("Diagnóstico • não executa o jogo");
        heading.setTextColor(Color.WHITE);
        heading.setTextSize(20);
        root.addView(heading);
        Button probe = new Button(this);
        probe.setText("Verificar memória e Vulkan");
        probe.setOnClickListener(v -> runProbe(false));
        root.addView(probe);
        Button frame = new Button(this);
        frame.setText("Apresentar quadro sintético");
        frame.setOnClickListener(v -> runProbe(true));
        root.addView(frame);
        surface = new SurfaceView(this);
        root.addView(surface, new LinearLayout.LayoutParams(-1,
                (int)(200 * getResources().getDisplayMetrics().density)));
        ScrollView scroll = new ScrollView(this);
        report = new TextView(this);
        report.setTextColor(Color.WHITE);
        report.setTextIsSelectable(true);
        scroll.addView(report);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        try {
            System.loadLibrary("las_android");
            loaded = true;
        } catch (LinkageError e) {
            report.setText("Biblioteca de diagnóstico indisponível: " + e.getMessage());
            probe.setEnabled(false);
            frame.setEnabled(false);
        }
    }

    private void runProbe(boolean present) {
        if (!loaded) return;
        Surface target = surface.getHolder().getSurface();
        if (present && (target == null || !target.isValid())) {
            report.append("Surface indisponível.\n");
            return;
        }
        report.append("Executando diagnóstico…\n");
        worker.execute(() -> {
            String result;
            try { result = present ? MainActivity.presentTestFrame(target) : MainActivity.probeHost(); }
            catch (Exception | LinkageError e) { result = e.toString(); }
            String message = result;
            runOnUiThread(() -> report.append(message + "\n"));
        });
    }

    @Override protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }
}
