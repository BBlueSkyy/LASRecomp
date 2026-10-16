package com.bblueskyy.lasrecomp;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** Synthetic ELF headers only; this exercises import transactions, never game execution. */
public final class GameModuleTest {
    private static void fixture(File root, int machine) throws IOException {
        root.mkdirs();
        byte[] h = new byte[64];
        h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
        h[4] = 2; h[5] = 1; h[6] = 1; h[16] = 3;
        h[18] = (byte)machine; h[19] = (byte)(machine >> 8);
        Files.write(new File(root, "lasgame.so").toPath(), h);
        Files.writeString(new File(root, "lasgame.so.id").toPath(), "0123456789abcdef\n");
    }
    private static void rejected(File incoming, File live) throws Exception {
        try { GameModule.publish(incoming, live); throw new AssertionError("accepted invalid module"); }
        catch (IOException expected) { }
        if (!new File(live, "old-module").isFile()) throw new AssertionError("previous module lost");
    }
    public static void main(String[] args) throws Exception {
        File base = Files.createTempDirectory("las-module-import-").toFile();
        try {
            File live = new File(base, "module"), incoming = new File(base, "incoming");
            live.mkdirs();
            Files.writeString(new File(live, "old-module").toPath(), "preserve");
            fixture(incoming, 62); // Host x86_64 must never be accepted as an Android module.
            rejected(incoming, live);
            fixture(incoming, 183);
            new File(incoming, "lasgame.so.id").delete();
            rejected(incoming, live);
            fixture(incoming, 183);
            Files.writeString(new File(incoming, "lasgame.so.id").toPath(), "wrong-version");
            rejected(incoming, live);
            fixture(incoming, 183);
            GameModule.publish(incoming, live);
            if (!GameModule.isPresent(live) || new File(live, "old-module").exists())
                throw new AssertionError("module was not atomically replaced");
            GameModule.validate(live);
            if (new File(live, "lasgame.so").canWrite() && !System.getProperty("user.name").equals("root"))
                throw new AssertionError("imported native code is writable");
            // Game-data imports may omit the optional module, but never include half a pair.
            File data = new File(base, "data"); data.mkdirs();
            GameModule.validateOptional(data);
            fixture(data, 183);
            new File(data, "lasgame.so.id").delete();
            try { GameModule.validateOptional(data); throw new AssertionError("accepted half a module"); }
            catch (IOException expected) { }
            System.out.println("GameModuleTest: PASS (synthetic headers, no guest code)");
        } finally { GameFiles.delete(base); }
    }
}
