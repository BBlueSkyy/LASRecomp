package com.bblueskyy.lasrecomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;

/** Versioned, immutable installations; interrupted staging trees are retained for diagnosis. */
final class EmbeddedAssets {
    interface Source {
        String[] list(String path) throws IOException;
        InputStream open(String path) throws IOException;
    }
    interface Progress { void update(String message); }

    static File install(Source source, File files, String identity, Progress progress) throws IOException {
        if (identity == null || !identity.matches("[A-Za-z0-9][A-Za-z0-9._-]{0,159}"))
            throw new IOException("Invalid embedded build identity");
        File parent = new File(files, "embedded");
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
        File destination = new File(parent, identity);
        File marker = new File(destination, ".complete");
        if (destination.exists()) {
            if (marker.isFile() && identity.equals(Files.readString(marker.toPath()))) return destination;
            throw new IOException("Incomplete embedded installation retained at " + destination);
        }
        File stage = new File(parent, identity + ".partial-" + UUID.randomUUID());
        if (!stage.mkdir()) throw new IOException("Cannot create " + stage);
        progress.update("Installing embedded Python and compiler…");
        copy(source, "embedded/python", new File(stage, "python"));
        copy(source, "embedded/compiler", new File(stage, "compiler"));
        try (FileOutputStream output = new FileOutputStream(new File(stage, ".complete"))) {
            output.write(identity.getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
        makeReadOnly(stage);
        if (!stage.renameTo(destination)) throw new IOException("Cannot activate embedded installation: " + stage);
        return destination;
    }

    private static void copy(Source source, String asset, File target) throws IOException {
        String[] children = source.list(asset);
        if (children.length > 0) {
            if (!target.mkdir()) throw new IOException("Cannot create " + target);
            for (String child : children) {
                if (child.isEmpty() || child.equals(".") || child.equals("..") || child.indexOf('/') >= 0
                        || child.indexOf('\\') >= 0) throw new IOException("Unsafe asset name: " + child);
                copy(source, asset + "/" + child, new File(target, child));
            }
        } else {
            try (InputStream input = source.open(asset); FileOutputStream output = new FileOutputStream(target)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                output.getFD().sync();
            }
        }
    }

    private static void makeReadOnly(File file) throws IOException {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot list " + file);
            for (File child : children) makeReadOnly(child);
        }
        if (!file.setWritable(false, false)) throw new IOException("Cannot make immutable: " + file);
    }
    private EmbeddedAssets() {}
}
