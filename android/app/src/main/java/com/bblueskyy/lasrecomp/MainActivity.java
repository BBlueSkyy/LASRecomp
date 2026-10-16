package com.bblueskyy.lasrecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.lang.ref.WeakReference;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android launcher and legal game-file import. The runtime has its own process. */
public final class MainActivity extends Activity {
    private static final int REQUEST_TREE = 42, REQUEST_DRIVER = 43;
    private static final ExecutorService IMPORTS = Executors.newSingleThreadExecutor();
    private static volatile boolean importing;
    private static volatile String importMessage = "";
    private static volatile WeakReference<MainActivity> active = new WeakReference<>(null);
    private TextView status;
    private Button select, play, log;

    // Existing diagnostic JNI entry points; launcher does not load runtime libraries.
    static native String probeHost();
    static native String presentTestFrame(android.view.Surface surface);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        active = new WeakReference<>(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int)(20 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(Color.rgb(19, 23, 29));
        TextView title = new TextView(this);
        title.setText("LASRecomp • Android ARM64");
        title.setTextColor(Color.WHITE);
        title.setTextSize(24);
        root.addView(title);
        TextView help = new TextView(this);
        help.setText("Importe uma cópia legal extraída de Link's Awakening v1.0.0. "
                + "Selecione a pasta que contém exefs/ e romfs/. Os arquivos serão copiados "
                + "para este aplicativo; reserve espaço livre para todos os dados.\n\n"
                + "Port experimental: a geração ARM64 dentro do aplicativo ainda está incompleta. "
                + "Importar os arquivos, sozinho, ainda não permite jogar nesta versão.");
        help.setTextColor(Color.LTGRAY);
        help.setPadding(0, pad, 0, pad);
        root.addView(help);
        select = new Button(this);
        select.setText("Importar / substituir arquivos do jogo");
        select.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_TREE);
        });
        root.addView(select);
        play = new Button(this);
        play.setText("Tentar iniciar o runtime");
        play.setOnClickListener(v -> {
            if (!GameFiles.isReady(gameRoot())) { refresh(); return; }
            Intent runtime = new Intent(this, GameActivity.class);
            if (BuildConfig.CUSTOM_VULKAN) {
                DriverImporter.addSelection(this, runtime);
                runtime.putExtra("configure_driver", true);
            }
            if (BuildConfig.EMBEDDED_SETUP) PreparationActivity.start(this, runtime);
            else startActivity(runtime);
        });
        root.addView(play);
        if (BuildConfig.EMBEDDED_SETUP) play.setText("Preparar e iniciar (experimental)");
        if (BuildConfig.CUSTOM_VULKAN) {
            Button driver = new Button(this);
            driver.setText("Importar e selecionar driver Turnip (.zip)");
            driver.setOnClickListener(v -> {
                Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                pick.setType("application/zip");
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivityForResult(pick, REQUEST_DRIVER);
            });
            root.addView(driver);
            Button system = new Button(this);
            system.setText("Usar driver do sistema");
            system.setOnClickListener(v -> { DriverImporter.selectSystem(this); update("Driver do sistema selecionado."); });
            root.addView(system);
        }
        Button diagnostic = new Button(this);
        diagnostic.setText("Diagnóstico do aparelho");
        diagnostic.setOnClickListener(v -> startActivity(new Intent(this, DiagnosticActivity.class)));
        root.addView(diagnostic);
        log = new Button(this);
        log.setText("Ver log da última inicialização");
        log.setOnClickListener(v -> showRuntimeLog());
        root.addView(log);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextIsSelectable(true);
        status.setPadding(0, pad, 0, 0);
        root.addView(status);
        ScrollView page = new ScrollView(this);
        page.setFillViewport(true);
        page.addView(root, new ScrollView.LayoutParams(-1, -2));
        setContentView(page);
        File previous = new File(getFilesDir(), "extracted.previous");
        if (!gameRoot().exists() && GameFiles.isReady(previous)) previous.renameTo(gameRoot());
        refresh();
    }

    private File gameRoot() { return new File(getFilesDir(), "extracted"); }

    @Override protected void onResume() {
        super.onResume();
        active = new WeakReference<>(this);
        refresh();
    }

    private void refresh() {
        if (status == null) return;
        boolean ready = GameFiles.isReady(gameRoot());
        select.setEnabled(!importing);
        play.setEnabled(ready && !importing);
        log.setEnabled(new File(getFilesDir(), "lasrecomp.log").isFile());
        status.setText(importMessage + (importMessage.isEmpty() ? "" : "\n\n")
                + (ready ? "Arquivos importados. Pronto para tentar iniciar o runtime."
                         : "Nenhuma importação completa. São necessários exefs/main, exefs/sdk e romfs/."));
    }

    private void showRuntimeLog() {
        TextView text = new TextView(this);
        text.setTextIsSelectable(true);
        text.setPadding(20, 20, 20, 20);
        try (RandomAccessFile file = new RandomAccessFile(new File(getFilesDir(), "lasrecomp.log"), "r")) {
            long end = file.length(), start = Math.max(0, end - 32768);
            file.seek(start);
            byte[] data = new byte[(int)(end - start)];
            file.readFully(data);
            text.setText((start > 0 ? "… últimas 32 KiB …\n" : "")
                    + new String(data, StandardCharsets.UTF_8));
        } catch (Exception e) { text.setText("Não foi possível ler o log: " + e.getMessage()); }
        ScrollView scroll = new ScrollView(this);
        scroll.addView(text);
        new AlertDialog.Builder(this).setTitle("Log do runtime").setView(scroll)
                .setPositiveButton("Fechar", null).show();
    }

    private static void update(String message) {
        importMessage = message;
        MainActivity activity = active.get();
        if (activity != null) activity.runOnUiThread(activity::refresh);
    }

    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if (request == REQUEST_DRIVER && result == RESULT_OK && intent != null && intent.getData() != null) {
            Uri uri = intent.getData();
            android.content.Context app = getApplicationContext();
            IMPORTS.execute(() -> {
                try {
                    DriverImporter.Driver driver = DriverImporter.importZip(app, uri);
                    DriverImporter.selectCustom(app, driver);
                    update("Driver selecionado: " + driver.name);
                } catch (Exception error) { update("Falha ao importar driver: " + error.getMessage()); }
            });
            return;
        }
        if (request != REQUEST_TREE || result != RESULT_OK || intent == null
                || intent.getData() == null || importing) return;
        Uri tree = intent.getData();
        importing = true;
        update("Importando… Mantenha o aplicativo aberto.");
        android.content.Context app = getApplicationContext();
        IMPORTS.execute(() -> {
            try {
                new SafImporter(app.getContentResolver(), (files, bytes) -> update(String.format(
                        Locale.getDefault(), "Importando: %d arquivos • %.1f MiB", files,
                        bytes / (1024.0 * 1024.0)))).importTree(tree, app.getFilesDir());
                update("Importação concluída.");
            } catch (Exception e) {
                update("Importação falhou: " + e.getMessage());
            } finally {
                importing = false;
                update(importMessage);
            }
        });
    }
}
