package com.bblueskyy.lasrecomp;

import android.content.Context;
import android.net.Uri;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashSet;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Imports only user-selected driver packages. No third-party driver is bundled. */
final class DriverImporter {
    static final String MODE = "driver_mode", DIRECTORY = "driver_directory", LIBRARY = "driver_library";
    static final String PREFERENCES = "graphics_driver";
    static final class Driver {
        final String name, directory, library;
        Driver(String name, String directory, String library) {
            this.name = name; this.directory = directory; this.library = library;
        }
    }

    /** Installation does not select a driver; the UI must explicitly store the user's choice. */
    static Driver importZip(Context context, Uri uri) throws Exception {
        File parent = new File(context.getFilesDir(), "drivers");
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create driver directory");
        String id = UUID.randomUUID().toString();
        File stage = new File(parent, ".partial-" + id);
        if (!stage.mkdir()) throw new IOException("Cannot create driver staging directory");
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("Document provider returned no data");
            extract(input, stage);
        }
        File meta = new File(stage, "meta.json");
        if (!meta.isFile() || meta.length() > 65536) throw new IOException("Missing or oversized meta.json");
        JSONObject metadata = new JSONObject(new String(Files.readAllBytes(meta.toPath()), StandardCharsets.UTF_8));
        String name = metadata.getString("name");
        String library = metadata.getString("libraryName");
        if (name.trim().isEmpty() || !library.matches("lib[A-Za-z0-9._-]+\\.so"))
            throw new IOException("Invalid driver name or libraryName");
        File binary = new File(stage, library);
        byte[] elf = new byte[20];
        try (FileInputStream input = new FileInputStream(binary)) {
            int offset = 0, read;
            while (offset < elf.length && (read = input.read(elf, offset, elf.length - offset)) > 0) offset += read;
            if (offset != elf.length || elf[0] != 127 || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F'
                    || elf[4] != 2 || elf[5] != 1 || (elf[18] & 255) != 183 || elf[19] != 0)
                throw new IOException("Driver must contain a 64-bit ARM ELF library");
        }
        immutable(stage);
        File installed = new File(parent, id);
        if (!stage.renameTo(installed)) throw new IOException("Cannot activate imported driver");
        return new Driver(name, installed.getAbsolutePath(), library);
    }

    static void selectSystem(Context context) {
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit().putString(MODE, "system")
                .remove(DIRECTORY).remove(LIBRARY).apply();
    }
    static void selectCustom(Context context, Driver driver) {
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).edit().putString(MODE, "custom")
                .putString(DIRECTORY, driver.directory).putString(LIBRARY, driver.library).apply();
    }
    static void addSelection(Context context, android.content.Intent intent) {
        android.content.SharedPreferences prefs = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        intent.putExtra(MODE, prefs.getString(MODE, "system"));
        intent.putExtra(DIRECTORY, prefs.getString(DIRECTORY, ""));
        intent.putExtra(LIBRARY, prefs.getString(LIBRARY, ""));
    }

    static void extract(InputStream input, File root) throws IOException {
        String prefix = root.getCanonicalPath() + File.separator;
        HashSet<String> paths = new HashSet<>();
        long total = 0;
        int entries = 0;
        try (ZipInputStream zip = new ZipInputStream(input)) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (++entries > 4096 || name.isEmpty() || name.startsWith("/") || name.contains("\\")
                        || name.indexOf('\0') >= 0) throw new IOException("Unsafe driver ZIP entry");
                for (String part : name.split("/")) if (part.equals("..") || part.equals("."))
                    throw new IOException("Driver ZIP path traversal");
                File target = new File(root, name);
                String canonical = target.getCanonicalPath();
                if (!canonical.startsWith(prefix) || !paths.add(canonical))
                    throw new IOException("Duplicate or unsafe driver ZIP path");
                if (entry.isDirectory()) {
                    if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create " + name);
                } else {
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
                    try (FileOutputStream output = new FileOutputStream(target)) {
                        int count;
                        while ((count = zip.read(buffer)) != -1) {
                            total += count;
                            if (total > 512L * 1024 * 1024) throw new IOException("Driver ZIP exceeds 512 MiB");
                            output.write(buffer, 0, count);
                        }
                        output.getFD().sync();
                    }
                }
                zip.closeEntry();
            }
        }
    }
    private static void immutable(File target) throws IOException {
        if (target.isDirectory()) {
            File[] entries = target.listFiles();
            if (entries == null) throw new IOException("Cannot list " + target);
            for (File entry : entries) immutable(entry);
        }
        if (!target.setWritable(false, false)) throw new IOException("Cannot protect " + target);
    }
    private DriverImporter() {}
}
