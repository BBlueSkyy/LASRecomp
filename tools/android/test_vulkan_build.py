#!/usr/bin/env python3
"""Cross-compile Android renderer dispatch without game assets or a GPU.

This is an object/link contract check, not an APK or rendering smoke test. It
also links the optional provider with --no-undefined and rejects system Vulkan
imports in the combined provider renderer objects.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def run(command, log):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(result.stdout)
    if result.returncode:
        raise SystemExit(f"Command failed ({result.returncode}); {log}\n{result.stdout[-6000:]}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_HOME"))
    parser.add_argument("--out", type=Path, default=ROOT / "build/android_vulkan_build_test")
    args = parser.parse_args()
    if args.ndk is None:
        parser.error("set ANDROID_NDK_HOME or pass --ndk (NDK r27c)")
    host = args.ndk.resolve() / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    deps = ROOT / "android/deps/arm64-v8a"
    src = ROOT / "android/deps/src"
    shaderc = deps / "include"
    if not (shaderc / "shaderc/shaderc.h").is_file():
        shaderc = src / "shaderc/libshaderc/include"
    required = [host / "aarch64-linux-android28-clang", host / "ld.lld",
                deps / "include/volk.h", deps / "lib/libadrenotools.so",
                deps / "include/adrenotools/driver.h", src / "volk/volk.c",
                deps / "include/vulkan/vulkan_core.h", src / "SDL/include/SDL3/SDL.h",
                shaderc / "shaderc/shaderc.h"]
    for path in required:
        if not path.is_file():
            parser.error(f"missing {path}; prepare Android source dependencies and run build-driver-deps.sh")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    includes = [ROOT / "runtime", ROOT / "runtime/nvn", deps / "include",
                src / "SDL/include", shaderc, ROOT / "third_party/imgui",
                ROOT / "third_party/imgui/backends"]
    common = ["-fPIC", "-fno-strict-aliasing", "-DVK_USE_PLATFORM_ANDROID_KHR=1",
              "-Wall", "-Wno-unused-function", "-Wno-format", "-Wno-nullability-completeness"]
    for path in includes:
        common += ["-I", path]
    renderer = [ROOT / "runtime/nvn/vulkan" / name for name in
                ("device.c", "textures.c", "exec.c", "shaders.c", "vma.cpp", "imgui_glue.cpp")]
    renderer += [ROOT / "third_party/imgui/backends/imgui_impl_vulkan.cpp"]
    for mode in ("system", "provider"):
        mode_out = out / mode
        mode_out.mkdir(exist_ok=True)
        sources = list(renderer)
        definitions = []
        if mode == "provider":
            definitions = ["-DLARECOMP_ANDROID_CUSTOM_VULKAN=1", "-DVK_NO_PROTOTYPES=1",
                           "-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES=1"]
            sources += [ROOT / "runtime/platform/android_vulkan.c", src / "volk/volk.c"]
        objects = []
        for source in sources:
            cpp = source.suffix == ".cpp"
            compiler = host / ("aarch64-linux-android28-clang++" if cpp else "aarch64-linux-android28-clang")
            obj = mode_out / (source.stem + ".o")
            run([compiler, "-std=c++17" if cpp else "-std=c11", *common, *definitions,
                 "-c", source, "-o", obj], mode_out / (source.stem + ".log"))
            objects.append(obj)
        combined = mode_out / "renderer.o"
        run([host / "ld.lld", "-r", *objects, "-o", combined], mode_out / "combine.log")
        undefined = run([host / "llvm-nm", "--undefined-only", combined], mode_out / "undefined.log")
        imports = re.findall(r"\bU (vk[A-Z]\w*)$", undefined, re.MULTILINE)
        if mode == "provider" and imports:
            raise SystemExit("provider renderer imports Vulkan symbols: " + ", ".join(imports))
        if mode == "system" and not imports:
            raise SystemExit("system renderer unexpectedly has no Vulkan imports")
        print(f"{mode}: {len(sources)} Android ARM64 renderer translation units compiled; "
              f"{len(imports)} unresolved Vulkan symbols")
    provider = out / "provider"
    run([host / "aarch64-linux-android28-clang", "-shared", provider / "android_vulkan.o",
         provider / "volk.o", "-L", deps / "lib", "-ladrenotools", "-ldl",
         "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384", "-o", provider / "libprovider_test.so"],
        provider / "link.log")
    dynamic = run([host / "llvm-readelf", "--dynamic", provider / "libprovider_test.so"],
                  provider / "dynamic.log")
    if re.search(r"\(NEEDED\).*\[libvulkan\.so\]", dynamic):
        raise SystemExit("optional provider links the system Vulkan loader")
    print("Optional provider linked with --no-undefined and without a libvulkan dependency.")
    print("This check does not create an Android surface or validate a game frame.")


if __name__ == "__main__":
    main()
