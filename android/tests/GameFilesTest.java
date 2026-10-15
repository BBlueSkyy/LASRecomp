package com.bblueskyy.lasrecomp;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** Host-side import transaction checks; no game data or Android device needed. */
public final class GameFilesTest {
    public static void main(String[] args) throws Exception {
        File base = Files.createTempDirectory("las-import-test").toFile();
        try {
            for (String bad : new String[]{"..", ".", "../escape", "a/b", "a\\b", "", "\0"}) {
                try { GameFiles.child(base, bad); throw new AssertionError("accepted unsafe name"); }
                catch (IOException expected) { }
            }
            File live = new File(base, "extracted");
            File invalid = new File(base, "incoming");
            live.mkdirs();
            Files.write(new File(live, "old-data").toPath(), new byte[]{1});
            invalid.mkdirs();
            try { GameFiles.publish(invalid, live); throw new AssertionError("accepted incomplete import"); }
            catch (IOException expected) { }
            if (!new File(live, "old-data").isFile()) throw new AssertionError("previous import lost");
            new File(invalid, "exefs").mkdirs();
            new File(invalid, "romfs/nested").mkdirs();
            byte[] main = new byte[0x100];
            main[0] = 'N'; main[1] = 'S'; main[2] = 'O'; main[3] = '0';
            Files.write(new File(invalid, "exefs/main").toPath(), main);
            Files.write(new File(invalid, "exefs/sdk").toPath(), new byte[]{'N', 'S', 'O', '0'});
            Files.write(new File(invalid, "romfs/nested/data").toPath(), new byte[]{3});
            try { GameFiles.publish(invalid, live); throw new AssertionError("accepted wrong game version"); }
            catch (IOException expected) { }
            if (!new File(live, "old-data").isFile()) throw new AssertionError("invalid version replaced old data");
            byte[] buildId = {(byte)0xae, 0x16, (byte)0xf7, 0x1e, 0x00, 0x2a, (byte)0xf8, (byte)0xcb};
            System.arraycopy(buildId, 0, main, 0x40, buildId.length);
            Files.write(new File(invalid, "exefs/main").toPath(), main);
            try { GameFiles.publish(invalid, live); throw new AssertionError("accepted truncated sdk header"); }
            catch (IOException expected) { }
            if (!new File(live, "old-data").isFile()) throw new AssertionError("truncated sdk replaced old data");
            byte[] sdk = new byte[0x100];
            sdk[0] = 'N'; sdk[1] = 'S'; sdk[2] = 'O'; sdk[3] = '0';
            Files.write(new File(invalid, "exefs/sdk").toPath(), sdk);
            GameFiles.publish(invalid, live);
            if (!GameFiles.isReady(live) || !new File(live, "romfs/nested/data").isFile())
                throw new AssertionError("nested import missing");
            if (new File(live, "old-data").exists()) throw new AssertionError("old files survived replacement");
            System.out.println("GameFilesTest: PASS");
        } finally { GameFiles.delete(base); }
    }
}
