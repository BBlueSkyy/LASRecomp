package com.bblueskyy.lasrecomp;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;

/** Streams SAF documents into app-private storage; SAF URIs are never native paths. */
final class SafImporter {
    interface Progress { void update(long files, long bytes); }
    private final ContentResolver resolver;
    private final Progress progress;
    private final Set<String> directories = new HashSet<>();
    private long files, bytes, lastReport;

    SafImporter(ContentResolver resolver, Progress progress) {
        this.resolver = resolver;
        this.progress = progress;
    }

    void importTree(Uri tree, File privateFiles) throws IOException {
        File staging = new File(privateFiles, "extracted.incoming");
        GameFiles.delete(staging);
        if (!staging.mkdirs()) throw new IOException("Não foi possível criar a pasta de importação.");
        try {
            copyDirectory(tree, DocumentsContract.getTreeDocumentId(tree), staging, 0, true);
            GameFiles.publish(staging, new File(privateFiles, "extracted"));
            progress.update(files, bytes);
        } catch (IOException | RuntimeException e) {
            try { GameFiles.delete(staging); } catch (IOException ignored) { }
            throw e;
        }
    }

    private void copyDirectory(Uri tree, String id, File destination, int depth,
            boolean root) throws IOException {
        if (depth > 128 || !directories.add(id))
            throw new IOException("A pasta contém um ciclo ou níveis demais.");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE};
        Set<String> names = new HashSet<>();
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Não foi possível listar a pasta selecionada.");
            while (cursor.moveToNext()) {
                String document = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (root && !"exefs".equals(name) && !"romfs".equals(name)) continue;
                if (!names.add(name)) throw new IOException("Nome de arquivo duplicado: " + name);
                File target = GameFiles.child(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!target.mkdir()) throw new IOException("Não foi possível criar " + name);
                    copyDirectory(tree, document, target, depth + 1, false);
                } else {
                    if (root) throw new IOException(name + " deve ser uma pasta.");
                    copyFile(DocumentsContract.buildDocumentUriUsingTree(tree, document), target);
                }
            }
        }
    }

    private void copyFile(Uri uri, File target) throws IOException {
        try (InputStream input = resolver.openInputStream(uri);
             FileOutputStream output = new FileOutputStream(target)) {
            if (input == null) throw new IOException("Não foi possível abrir " + target.getName());
            byte[] buffer = new byte[1024 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                bytes += count;
                reportProgress();
            }
            output.getFD().sync();
            files++;
            reportProgress();
        }
    }

    private void reportProgress() {
        long now = android.os.SystemClock.elapsedRealtime();
        if (now - lastReport > 300) {
            lastReport = now;
            progress.update(files, bytes);
        }
    }
}
