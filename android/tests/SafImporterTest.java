package com.bblueskyy.lasrecomp;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract.Document;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Synthetic SAF provider exercising the real streaming importer; no Android or game execution. */
public final class SafImporterTest {
    private static final class Provider extends ContentResolver {
        final Map<String, List<String[]>> children = new HashMap<>();
        final Map<String, byte[]> contents = new HashMap<>();
        final List<String> opened = new ArrayList<>();
        String failure;
        void add(String parent, String id, String name, byte[] data) {
            children.computeIfAbsent(parent, key -> new ArrayList<>()).add(new String[]{id, name,
                    data == null ? Document.MIME_TYPE_DIR : "application/octet-stream"});
            if (data != null) contents.put(id, data);
        }
        @Override public Cursor query(Uri uri, String[] columns, String selection, String[] args, String order) {
            List<String[]> rows = children.getOrDefault(uri.id, new ArrayList<>());
            return new Cursor() {
                int at = -1;
                public boolean moveToNext() { return ++at < rows.size(); }
                public String getString(int column) { return rows.get(at)[column]; }
                public void close() { }
            };
        }
        @Override public InputStream openInputStream(Uri uri) throws IOException {
            opened.add(uri.id);
            if (uri.id.equals(failure)) throw new IOException("Synthetic provider disconnected");
            return new ByteArrayInputStream(contents.get(uri.id));
        }
    }
    private static Provider tree(boolean wrongCpu) {
        Provider p = new Provider();
        byte[] h = new byte[64];
        h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
        h[4] = 2; h[5] = 1; h[6] = 1; h[16] = 3; h[18] = (byte)(wrongCpu ? 62 : 183);
        p.add("tree", "module", "lasgame.so", h);
        p.add("tree", "id", "lasgame.so.id", "0123456789abcdef\n".getBytes(StandardCharsets.US_ASCII));
        p.add("tree", "ignored", "huge-unrelated-file", new byte[]{1});
        p.add("tree", "romfs", "romfs", null);
        p.add("romfs", "nested", "region_common", null);
        p.add("nested", "asset", "asset", new byte[]{3});
        p.add("tree", "exefs", "exefs", null);
        byte[] main = new byte[256];
        main[0] = 'N'; main[1] = 'S'; main[2] = 'O'; main[3] = '0';
        byte[] id = {(byte)0xae, 0x16, (byte)0xf7, 0x1e, 0, 0x2a, (byte)0xf8, (byte)0xcb};
        System.arraycopy(id, 0, main, 0x40, id.length);
        p.add("exefs", "main", "main", main);
        p.add("exefs", "sdk", "sdk", main);
        return p;
    }
    private static SafImporter importer(Provider provider) { return new SafImporter(provider, (files, bytes) -> { }); }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("las-saf-test-").toFile();
        try {
            Provider p = tree(false);
            importer(p).importModuleTree(new Uri("tree"), root);
            if (!GameModule.isPresent(new File(root, "module"))) throw new AssertionError("module missing");
            if (p.opened.size() != 2 || p.opened.contains("asset"))
                throw new AssertionError("module import copied unrelated game data");
            byte[] previous = Files.readAllBytes(new File(root, "module/lasgame.so").toPath());
            for (Provider bad : new Provider[]{tree(true), tree(false)}) {
                if (bad.contents.get("module")[18] != 62) bad.failure = "id";
                try { importer(bad).importModuleTree(new Uri("tree"), root); throw new AssertionError("invalid import succeeded"); }
                catch (IOException expected) { }
                if (!java.util.Arrays.equals(previous, Files.readAllBytes(new File(root, "module/lasgame.so").toPath())))
                    throw new AssertionError("failed import replaced the previous module");
                if (new File(root, "module.incoming").exists()) throw new AssertionError("failed staging was not cleaned");
            }
            p = tree(false);
            importer(p).importTree(new Uri("tree"), root);
            File extracted = new File(root, "extracted");
            if (!GameFiles.isReady(extracted) || !GameModule.isPresent(extracted)
                    || !new File(extracted, "romfs/region_common/asset").isFile())
                throw new AssertionError("full SAF tree did not preserve optional module and nested data");
            if (p.opened.contains("ignored")) throw new AssertionError("copied unrelated root item");
            System.out.println("SafImporterTest: PASS (synthetic provider and ELF headers only)");
        } finally { GameFiles.delete(root); }
    }
}
