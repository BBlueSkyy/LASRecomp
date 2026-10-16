#!/usr/bin/env python3
"""Package public target headers, API28 link stubs, CRT and compiler-rt only.

Recovered checkpoint: header-closure and packaged compiler execution have not
been verified. No compiler executable or full NDK is placed in the APK.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def package(ndk: Path, llvm: Path, out: Path) -> None:
    root = Path(__file__).resolve().parents[2]
    host = ndk / "toolchains/llvm/prebuilt/linux-x86_64"
    cc = host / "bin/aarch64-linux-android28-clang"
    sysroot = host / "sysroot"
    resource = llvm / "clang/lib/Headers"
    if not cc.is_file() or not (resource / "stddef.h").is_file():
        raise ValueError("expected an NDK r27c toolchain and LLVM 18.1.8 source checkout")
    with tempfile.TemporaryDirectory(prefix="compiler-assets-") as temporary:
        stage = Path(temporary)
        probe = stage / "probe.c"
        probe.write_text('#include "gamelib.h"\n#include "simd.h"\n')
        command = [str(cc), "-std=gnu11", "-M", "-MT", "probe", "-nostdinc",
                   "-isystem", str(resource), "-isystem", str(sysroot / "usr/include/aarch64-linux-android"),
                   "-isystem", str(sysroot / "usr/include"), "-I", str(root / "runtime"), str(probe)]
        dependencies = subprocess.check_output(command, text=True).replace("\\\n", " ")
        headers = shlex.split(dependencies.partition(":")[2])
        payload = stage / "compiler"
        for value in headers:
            source = Path(value).resolve()
            if source == probe:
                continue
            if source.is_relative_to(resource):
                destination = payload / "resource/include" / source.relative_to(resource)
            elif source.is_relative_to(sysroot):
                destination = payload / "sysroot" / source.relative_to(sysroot)
            elif source.is_relative_to(root / "runtime"):
                destination = payload / "runtime" / source.relative_to(root / "runtime")
            else:
                raise ValueError(f"unexpected target header outside public toolchain: {source}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        for name in ("cpu.h", "simd.h", "simd_fp.h", "gamelib.h", "gamelib_contract.h"):
            shutil.copyfile(root / "runtime" / name, payload / "runtime" / name)
        shutil.copyfile(root / "tools/gamelib_entry.c", payload / "gamelib_entry.c")
        library = Path("usr/lib/aarch64-linux-android/28")
        for name in ("crtbegin_so.o", "crtend_so.o", "libc.so", "libm.so", "libdl.so"):
            destination = payload / "sysroot" / library / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sysroot / library / name, destination)
        ndk_resource = Path(subprocess.check_output([str(cc), "-print-resource-dir"], text=True).strip())
        builtins = "libclang_rt.builtins-aarch64-android.a"
        destination = payload / "resource/lib/linux" / builtins
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ndk_resource / "lib/linux" / builtins, destination)
        licenses = payload / "licenses"
        licenses.mkdir()
        shutil.copyfile(llvm / "LICENSE.TXT", licenses / "LLVM-LICENSE.TXT")
        notice = ndk / "NOTICE"
        if notice.is_file():
            shutil.copyfile(notice, licenses / "NDK-NOTICE.txt")
        manifest = {str(path.relative_to(payload)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(payload.rglob("*")) if path.is_file()}
        (payload / "manifest.json").write_text(json.dumps({"llvm": "18.1.8", "ndk": "r27c", "api": 28,
                                                          "files": manifest}, indent=2) + "\n")
        if out.exists():
            shutil.rmtree(out)
        shutil.copytree(payload, out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--llvm-source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    package(args.ndk.resolve(), args.llvm_source.resolve(), args.out.resolve())
