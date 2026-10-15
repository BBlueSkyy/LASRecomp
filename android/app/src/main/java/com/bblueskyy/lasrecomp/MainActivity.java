package com.bblueskyy.lasrecomp;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int REQUEST_TREE = 42;
    private static final String KEY_TREE = "tree";
    static { System.loadLibrary("las_android"); }
    private static native String probeHost();
    private static native String presentTestFrame(Surface surface);
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView report;
    private SurfaceView surfaceView;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 22, 24, 22);
        root.setBackgroundColor(Color.rgb(19, 23, 29));

        TextView heading = new TextView(this);
        heading.setText("LASRecomp • Android ARM64");
        heading.setTextColor(Color.WHITE);
        heading.setTextSize(22);
        root.addView(heading);
        TextView warning = new TextView(this);
        warning.setText("APENAS diagnóstico. Ainda não executa o jogo.");
        warning.setTextColor(Color.YELLOW);
        root.addView(warning);

        Button test = new Button(this);
        test.setText("Testar Vulkan e memória guest");
        root.addView(test);
        test.setOnClickListener(v -> invoke("Diagnóstico", () -> probeHost()));

        Button present = new Button(this);
        present.setText("Apresentar um quadro Vulkan de teste");
        root.addView(present);
        present.setOnClickListener(v -> {
            Surface s = surfaceView.getHolder().getSurface();
            if (s == null || !s.isValid()) {
                report.append("Surface ainda indisponível.\n");
                return;
            }
            invoke("Quadro sintético", () -> presentTestFrame(s));
        });

        Button select = new Button(this);
        select.setText("Selecionar arquivos extraídos v1.0.0");
        root.addView(select);
        select.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(i, REQUEST_TREE);
        });

        surfaceView = new SurfaceView(this);
        root.addView(surfaceView, new LinearLayout.LayoutParams(-1,
                (int)(200 * getResources().getDisplayMetrics().density)));
        ScrollView scroll = new ScrollView(this);
        report = new TextView(this);
        report.setTextColor(Color.WHITE);
        report.setTextIsSelectable(true);
        report.setPadding(6, 14, 6, 14);
        scroll.addView(report);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        report.setText("Port em andamento. O teste não contém dados do jogo.\n");
        String prior = getPreferences(MODE_PRIVATE).getString(KEY_TREE, "");
        if (!prior.isEmpty()) report.append("Pasta SAF registrada: " + prior + "\n");
    }

    private interface Probe { String execute(); }
    private void invoke(String title, Probe probe) {
        report.append("\n" + title + "...\n");
        executor.execute(() -> {
            String result;
            try { result = probe.execute(); }
            catch (Exception | LinkageError e) { result = e.toString(); }
            final String message = result;
            runOnUiThread(() -> report.append(message + "\n"));
        });
    }

    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if (request != REQUEST_TREE || result != RESULT_OK
                || intent == null || intent.getData() == null) return;
        Uri tree = intent.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            getPreferences(MODE_PRIVATE).edit().putString(KEY_TREE, tree.toString()).apply();
            report.append("Pasta selecionada: " + tree + "\n" + checkTree(tree));
        } catch (Exception e) {
            report.append("Acesso SAF falhou: " + e + "\n");
        }
    }

    private String checkTree(Uri tree) {
        try {
            String id = DocumentsContract.getTreeDocumentId(tree);
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
            Set<String> names = new HashSet<>();
            try (Cursor c = getContentResolver().query(children,
                    new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME},
                    null, null, null)) {
                if (c != null) while (c.moveToNext()) names.add(c.getString(0));
            }
            return names.contains("exefs") && names.contains("romfs")
                ? "exefs/ e romfs/ encontrados. Importação ainda não implementada.\n"
                : "Pasta não contém exefs/ e romfs/.\n";
        } catch (Exception e) {
            return "Não foi possível verificar a pasta: " + e + "\n";
        }
    }

    @Override protected void onDestroy() {
        executor.shutdown();
        super.onDestroy();
    }
}
