#!/usr/bin/env python3
"""One Android generated-module ABI fingerprint, shared by CMake and the builder."""
import argparse
import hashlib
from pathlib import Path

# Same semantic inputs as Makefile's GAMELIB_SRCS. Include the Android build
# contract as well: changing these flags invalidates already generated modules.
SOURCES = (
    "tools/gen.py", "tools/lifter.py", "tools/analyze.py", "tools/nso.py",
    "tools/passes.py", "tools/patches.py", "tools/simd_c.py",
    "tools/rando/patch_variants.json", "tools/gamelib_entry.c",
    "runtime/hle/hle_symbols.txt", "runtime/cpu.h", "runtime/simd.h",
    "runtime/simd_fp.h", "runtime/gamelib.h",
    "tools/android/embedded_generate.py", "tools/android/compilation_cache.py",
    "android/app/src/main/cpp/embedded_compiler.cpp", "android/app/src/main/cpp/python_generator.c",
)
COMPILE_FLAGS = (
    "-O1", "-fPIC", "-fno-strict-aliasing", "-fvisibility=hidden",
    "-Wno-unused-label", "-Wno-return-type", "-mno-outline-atomics",
)
LINK_FLAGS = (
    "-shared", "-Wl,--no-undefined", "-Wl,-z,relro,-z,now,-z,noexecstack",
    "-Wl,-z,max-page-size=16384", "-Wl,-z,common-page-size=16384",
    "-Wl,-soname,lasgame.so",
)
CONTRACT = ("android-arm64-v8a;api>=28;simd=asm;main=0x7000000000;sdk=0x7004000000;"
            "runtime=liblasrecomp.so;" + " ".join(COMPILE_FLAGS + LINK_FLAGS))


def gamelib_id(root: Path) -> str:
    digest = hashlib.sha256()
    for name in SOURCES:
        data = (root / name).read_bytes()
        digest.update(name.encode() + b"\0")
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
    digest.update(CONTRACT.encode())
    return digest.hexdigest()[:16]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    print(gamelib_id(args.root))


if __name__ == "__main__":
    main()
