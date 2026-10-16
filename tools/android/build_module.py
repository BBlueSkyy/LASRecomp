#!/usr/bin/env python3
"""Build the real ARM64 Android module from an owner's extracted NSOs.

This is a host development/validation tool, not the APK's on-device generator.
It uses the unmodified generator and an installed Android NDK; neither a desktop
Python executable nor an NDK host executable can be run inside an Android app.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from gamelib_id import COMPILE_FLAGS, LINK_FLAGS, gamelib_id
from address_layout import ANDROID_GUEST_BASE, SDK_OFFSET, guest_base


def run(args: list[str], **kwargs) -> None:
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def compiler(ndk: Path, api: int) -> Path:
    name = f"aarch64-linux-android{api}-clang"
    candidates = list((ndk / "toolchains/llvm/prebuilt").glob(f"*/bin/{name}"))
    if sys.platform == "win32":
        candidates += list((ndk / "toolchains/llvm/prebuilt").glob(f"*/bin/{name}.cmd"))
    if len(candidates) != 1:
        raise ValueError(f"expected one NDK compiler {name}; found {len(candidates)} in {ndk}")
    return candidates[0]


def validate_elf(path: Path) -> None:
    # Detailed runtime validation is in runtime/gamelib_validate.c. Refuse the
    # most common accidental outputs here before publishing either file.
    with path.open("rb") as stream:
        header = stream.read(64)
    if len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("compiler did not produce a little-endian ELF64 module")
    if struct.unpack_from("<HH", header, 16) != (3, 183):
        raise ValueError("compiler did not produce an AArch64 ET_DYN shared library")


def build(args: argparse.Namespace) -> Path:
    root = Path(__file__).resolve().parents[2]
    cc = compiler(args.ndk.resolve(), args.api)
    game = args.game_dir.resolve()
    runtime = args.runtime_lib.resolve()
    if runtime.name != "liblasrecomp.so" or not runtime.is_file():
        raise ValueError("--runtime-lib must point to the APK build's liblasrecomp.so")
    validate_elf(runtime)
    for name in ("main", "sdk"):
        path = game / "exefs" / name
        with path.open("rb") as stream:
            if stream.read(4) != b"NSO0":
                raise ValueError(f"{path} is not an extracted NSO")
    # Validate actual imports before starting a potentially lengthy generation.
    run([sys.executable, "-c", "import unicorn; import capstone; import lz4.block"], cwd=root / "tools")
    identity = gamelib_id(root, args.guest_base)
    output = (args.out or game / "lasgame.so").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lasgame-", dir=args.work_dir) as temp:
        work = Path(temp)
        for name, base in (("main", hex(args.guest_base)), ("sdk", hex(args.guest_base + SDK_OFFSET))):
            command = [sys.executable, root / "tools/gen.py", name, game / "exefs" / name,
                       base, work / name, "--simd", "asm"]
            if name == "sdk":
                command += ["--hle", root / "runtime/hle/hle_symbols.txt"]
            run(command, cwd=root)
        sources = [root / "tools/gamelib_entry.c"]
        for name in ("main", "sdk"):
            chunks = sorted((work / name).glob(f"{name}_[0-9]*.c"))
            if not chunks or not (work / name / f"{name}_table.c").is_file():
                raise ValueError(f"generator did not produce a complete {name} module")
            sources += chunks + [work / name / f"{name}_table.c"]
        objects = [work / f"unit-{i:05}.o" for i in range(len(sources))]

        def compile_unit(pair):
            source, obj = pair
            run([cc, *COMPILE_FLAGS, f'-DGAMELIB_ID="{identity}"', "-I", root / "runtime",
                 "-c", source, "-o", obj])

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(compile_unit, zip(sources, objects)))
        # A response file avoids ARG_MAX for the full game's generated chunks.
        response = work / "objects.rsp"
        response.write_text("".join('"' + str(p).replace("\\", "\\\\").replace('"', '\\"') + '"\n'
                                    for p in objects))
        temporary = work / "lasgame.so"
        run([cc, *LINK_FLAGS, "-o", temporary, f"@{response}", runtime, "-lm"])
        validate_elf(temporary)
        # Stage on the output filesystem so replacement itself is atomic.
        descriptor, staged = tempfile.mkstemp(prefix=".lasgame-", suffix=".so", dir=output.parent)
        os.close(descriptor)
        try:
            shutil.copyfile(temporary, staged)
            os.chmod(staged, 0o444)
            os.replace(staged, output)
        finally:
            if os.path.exists(staged):
                os.unlink(staged)
        # Write the sidecar only after the complete module has been published.
        sidecar = output.with_name(output.name + ".id")
        sidecar.write_text(identity + "\n")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest-base", type=guest_base, default=ANDROID_GUEST_BASE,
                        help="identity-address layout; must match the APK CMake LARECOMP_GUEST_BASE")
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_HOME"), required=not os.environ.get("ANDROID_NDK_HOME"))
    parser.add_argument("--runtime-lib", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work-dir", type=Path, help="existing directory for temporary generated C and objects")
    parser.add_argument("--api", type=int, default=28)
    parser.add_argument("--jobs", type=int, default=2, help="compiler jobs (large generated units use substantial memory)")
    args = parser.parse_args()
    if args.api < 28 or args.jobs < 1 or args.jobs > 64:
        parser.error("--api must be >=28 and --jobs must be between 1 and 64")
    try:
        print(build(args))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"module build failed: {error}\n")


if __name__ == "__main__":
    main()
