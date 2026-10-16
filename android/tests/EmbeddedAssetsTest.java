package com.bblueskyy.lasrecomp;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.Map;

public final class EmbeddedAssetsTest {
    static EmbeddedAssets.Source source(Map<String, String> values) {
        return new EmbeddedAssets.Source() {
            public String[] list(String path) {
                return values.keySet().stream().filter(key -> key.startsWith(path + "/"))
                        .map(key -> key.substring(path.length() + 1).split("/")[0]).distinct().toArray(String[]::new);
            }
            public InputStream open(String path) throws IOException {
                if (!values.containsKey(path)) throw new IOException("Missing " + path);
                return new ByteArrayInputStream(values.get(path).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
        };
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("las-assets-test").toFile();
        EmbeddedAssets.Source first = source(Map.of("embedded/python/stdlib.zip", "python-v1",
                "embedded/compiler/include/header.h", "compiler-v1"));
        File v1 = EmbeddedAssets.install(first, root, "v1", ignored -> {});
        if (!Files.readString(new File(v1, "python/stdlib.zip").toPath()).equals("python-v1"))
            throw new AssertionError("Asset content differs");
        EmbeddedAssets.Source unavailable = source(Map.of());
        if (!EmbeddedAssets.install(unavailable, root, "v1", ignored -> {}).equals(v1))
            throw new AssertionError("Complete installation was not reused");
        try {
            EmbeddedAssets.install(unavailable, root, "v2", ignored -> {});
            throw new AssertionError("Missing compiler assets accepted");
        } catch (IOException expected) {}
        if (new File(root, "embedded/v2").exists()) throw new AssertionError("Partial published");
        if (new File(root, "embedded").listFiles(file -> file.getName().startsWith("v2.partial-")).length != 1)
            throw new AssertionError("Failure not retained");
        try {
            EmbeddedAssets.install(first, root, "../outside", ignored -> {});
            throw new AssertionError("Identity traversal accepted");
        } catch (IOException expected) {}
        System.out.println("EmbeddedAssetsTest passed");
    }
}
