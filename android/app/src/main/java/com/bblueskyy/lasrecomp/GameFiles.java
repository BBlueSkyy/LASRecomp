package com.bblueskyy.lasrecomp;

import java.io.File;
import java.io.IOException;
import java.io.FileInputStream;

/** Private game-data layout, with replacement only after a complete import. */
final class GameFiles {
    private GameFiles() { }

    static File child(File parent, String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("Nome de arquivo inválido na pasta selecionada.");
        File child = new File(parent, name);
        if (!child.getCanonicalFile().getParentFile().equals(parent.getCanonicalFile()))
            throw new IOException("Arquivo fora da pasta de importação.");
        return child;
    }

    static boolean isReady(File root) {
        File main = new File(root, "exefs/main"), sdk = new File(root, "exefs/sdk");
        return main.isFile() && main.length() > 0 && sdk.isFile() && sdk.length() > 0
                && new File(root, "romfs").isDirectory();
    }

    static void publish(File incoming, File live) throws IOException {
        if (!isReady(incoming))
            throw new IOException("São necessários exefs/main, exefs/sdk e a pasta romfs/.");
        validateNso(new File(incoming, "exefs/main"), true);
        validateNso(new File(incoming, "exefs/sdk"), false);
        GameModule.validateOptional(incoming);
        File module = new File(incoming, "lasgame.so");
        if (module.exists() && !module.setReadOnly())
            throw new IOException("Não foi possível proteger o módulo importado contra escrita.");
        File backup = new File(live.getParentFile(), "extracted.previous");
        if (backup.exists()) delete(backup);
        boolean hadLive = live.exists();
        if (hadLive && !live.renameTo(backup))
            throw new IOException("Não foi possível preservar a importação anterior.");
        if (!incoming.renameTo(live)) {
            if (hadLive && !backup.renameTo(live))
                throw new IOException("Falha ao concluir a importação; dados anteriores em extracted.previous.");
            throw new IOException("Não foi possível concluir a importação.");
        }
        // A leftover backup is harmless and can be cleaned up on the next import.
        try { delete(backup); } catch (IOException ignored) { }
    }

    private static void validateNso(File file, boolean main) throws IOException {
        byte[] header = new byte[0x100];
        try (FileInputStream stream = new FileInputStream(file)) {
            int used = 0, count;
            while (used < header.length && (count = stream.read(header, used, header.length - used)) != -1)
                used += count;
            if (used != header.length || header[0] != 'N' || header[1] != 'S'
                    || header[2] != 'O' || header[3] != '0')
                throw new IOException("exefs/" + file.getName() + " não é um NSO extraído válido.");
        }
        if (main) {
            byte[] expected = {(byte)0xae, 0x16, (byte)0xf7, 0x1e, 0x00, 0x2a, (byte)0xf8, (byte)0xcb};
            for (int i = 0; i < expected.length; i++)
                if (header[0x40 + i] != expected[i])
                    throw new IOException("Versão incompatível: use Link's Awakening v1.0.0 "
                            + "(Build ID ae16f71e002af8cb).");
        }
    }

    static void delete(File file) throws IOException {
        if (!file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Não foi possível ler " + file.getName());
            for (File child : children) delete(child);
        }
        if (!file.delete()) throw new IOException("Não foi possível remover " + file.getName());
    }
}
