package com.bblueskyy.lasrecomp;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Private, owner-generated Android module. Full ELF/ABI validation happens in the native loader. */
final class GameModule {
    private GameModule() { }

    static boolean isPresent(File root) {
        return new File(root, "lasgame.so").isFile() && new File(root, "lasgame.so.id").isFile();
    }

    static void validateOptional(File root) throws IOException {
        if (new File(root, "lasgame.so").exists() || new File(root, "lasgame.so.id").exists()) validate(root);
    }

    static void validate(File root) throws IOException {
        if (!isPresent(root)) throw new IOException("São necessários lasgame.so e lasgame.so.id na pasta selecionada.");
        byte[] header = new byte[64];
        try (FileInputStream input = new FileInputStream(new File(root, "lasgame.so"))) {
            int used = 0, count;
            while (used < header.length && (count = input.read(header, used, header.length - used)) != -1)
                used += count;
            if (used != header.length || header[0] != 0x7f || header[1] != 'E'
                    || header[2] != 'L' || header[3] != 'F' || header[4] != 2 || header[5] != 1
                    || header[6] != 1 || header[16] != 3 || header[17] != 0
                    || (header[18] & 255) != 183 || header[19] != 0)
                throw new IOException("lasgame.so deve ser uma biblioteca ELF64 ARM64 para Android.");
        }
        File sidecar = new File(root, "lasgame.so.id");
        if (sidecar.length() < 16 || sidecar.length() > 18)
            throw new IOException("lasgame.so.id inválido; importe o par produzido para este APK.");
        byte[] id = new byte[(int)sidecar.length()];
        try (FileInputStream input = new FileInputStream(sidecar)) {
            if (input.read(id) != id.length || !new String(id, StandardCharsets.US_ASCII).matches("[0-9a-f]{16}(\\r?\\n)?"))
                throw new IOException("lasgame.so.id inválido; importe o par produzido para este APK.");
        }
    }

    static void publish(File incoming, File live) throws IOException {
        validate(incoming);
        if (!new File(incoming, "lasgame.so").setReadOnly())
            throw new IOException("Não foi possível proteger o módulo importado contra escrita.");
        File backup = new File(live.getParentFile(), "module.previous");
        GameFiles.delete(backup);
        boolean hadLive = live.exists();
        if (hadLive && !live.renameTo(backup)) throw new IOException("Não foi possível preservar o módulo anterior.");
        if (!incoming.renameTo(live)) {
            if (hadLive && !backup.renameTo(live))
                throw new IOException("Falha na importação; módulo anterior em module.previous.");
            throw new IOException("Não foi possível concluir a importação do módulo.");
        }
        try { GameFiles.delete(backup); } catch (IOException ignored) { }
    }
}
