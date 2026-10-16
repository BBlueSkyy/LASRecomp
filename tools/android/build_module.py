#!/usr/bin/env python3
"""Build the real ARM64 Android module from an owner's extracted NSOs.

This is a host development/validation tool, not the APK's on-device generator.
It uses the unmodified generator and an installed Android NDK; neither a desktop
Python executable nor an NDK host executable can be run inside an Android app.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

from gamelib_id import COMPILE_FLAGS, LINK_FLAGS, gamelib_id
from address_layout import SDK_OFFSET, guest_base


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


def runtime_contract(path: Path) -> dict:
    """Read the APK's exported ABI data, never dlopen an ARM64 library on the host."""
    validate_elf(path)
    if path.stat().st_size > 512 * 1024 * 1024:
        raise ValueError('runtime ELF exceeds 512 MiB')
    data = path.read_bytes()

    def region(offset, size):
        if offset > len(data) or size > len(data) - offset:
            raise ValueError('runtime ELF table is outside the file')
        return data[offset:offset + size]

    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    phsize, phnum, shsize, shnum = struct.unpack_from('<HHHH', data, 54)
    if phsize < 56 or shsize < 64 or not phnum or not shnum:
        raise ValueError('runtime ELF has no readable build contract tables')
    region(phoff, phsize * phnum)
    region(shoff, shsize * shnum)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shsize) for i in range(shnum)]
    contracts = []
    for section in sections:
        _, kind, _, _, offset, size, link, _, _, entry_size = section
        if kind != 11:  # SHT_DYNSYM, retained when Android strips debug symbols
            continue
        if entry_size < 24 or size % entry_size or link >= shnum:
            raise ValueError('invalid runtime ELF dynamic symbol table')
        region(offset, size)
        strings = sections[link]
        if strings[1] != 3:
            raise ValueError('runtime ELF symbols have no string table')
        names = region(strings[4], strings[5])
        for index in range(size // entry_size):
            name, info, other, defined, value, symbol_size = struct.unpack_from('<IBBHQQ', data, offset + index * entry_size)
            if name >= len(names):
                raise ValueError('invalid runtime ELF symbol name')
            end = names.find(b'\0', name)
            if end < 0:
                raise ValueError('unterminated runtime ELF symbol name')
            if names[name:end] != b'lasrecomp_build_contract_v1':
                continue
            if not defined or info & 15 != 1 or info >> 4 not in (1, 2) or other & 3 != 0 or symbol_size < 41:
                raise ValueError('invalid runtime build contract symbol')
            for i in range(phnum):
                kind, flags, file_offset, address, _, file_size, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
                if kind == 1 and flags & 4 and value >= address and value - address <= file_size:
                    delta = value - address
                    if symbol_size <= file_size - delta:
                        contracts.append(region(file_offset + delta, symbol_size))
    if len(contracts) != 1:
        raise ValueError('APK runtime lacks a unique build contract; rebuild the APK from this branch')
    blob = contracts[0]
    magic, version, abi, size, base, identity = struct.unpack_from('<4sIIIQ17s', blob)
    if magic != b'LASC' or version != 1 or abi != 1 or size != len(blob) or not re.fullmatch(b'[0-9a-f]{16}\0', identity):
        raise ValueError('unsupported runtime build contract')
    return {'abi': abi, 'guest_base': guest_base(base), 'id': identity[:-1].decode('ascii')}


def match_runtime(path: Path, root: Path, requested_base=None):
    contract = runtime_contract(path)
    base = contract['guest_base']
    if requested_base is not None and requested_base != base:
        raise ValueError(f'guest layout mismatch: APK uses {base:#x}, requested {requested_base:#x}')
    identity = gamelib_id(root, base)
    if contract['id'] != identity:
        raise ValueError('APK and current generator/ABI sources differ; check out the APK commit or rebuild the APK')
    return base, identity


def runtime_from_apk(apk: Path, directory: Path) -> Path:
    """Read only the installed runtime ABI; never import game code from an APK."""
    name = 'lib/arm64-v8a/liblasrecomp.so'
    with zipfile.ZipFile(apk) as archive:
        entries = [entry for entry in archive.infolist() if entry.filename == name]
        if len(entries) != 1 or entries[0].is_dir() or not 64 <= entries[0].file_size <= 512 * 1024 * 1024:
            raise ValueError('APK must contain exactly one ARM64 liblasrecomp.so (up to 512 MiB)')
        library = directory / 'liblasrecomp.so'
        with archive.open(entries[0]) as source, library.open('wb') as output:
            shutil.copyfileobj(source, output, length=1024 * 1024)
    validate_elf(library)
    return library


def build(args: argparse.Namespace) -> Path:
    root = Path(__file__).resolve().parents[2]
    game = args.game_dir.resolve()
    runtime = args.runtime_lib.resolve()
    if runtime.name != "liblasrecomp.so" or not runtime.is_file():
        raise ValueError("--runtime-lib must point to the APK build's liblasrecomp.so")
    args.guest_base, identity = match_runtime(runtime, root, args.guest_base)
    cc = compiler(args.ndk.resolve(), args.api)
    for name in ("main", "sdk"):
        path = game / "exefs" / name
        with path.open("rb") as stream:
            if stream.read(4) != b"NSO0":
                raise ValueError(f"{path} is not an extracted NSO")
    # Validate actual imports before starting a potentially lengthy generation.
    run([sys.executable, "-c", "import unicorn; import capstone; import lz4.block"], cwd=root / "tools")
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
    parser.add_argument("--guest-base", type=guest_base,
                        help="optional layout assertion; otherwise use the APK's exported guest base")
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_HOME"), required=not os.environ.get("ANDROID_NDK_HOME"))
    runtime = parser.add_mutually_exclusive_group(required=True)
    runtime.add_argument("--runtime-lib", type=Path, help="installed ARM64 liblasrecomp.so from this APK build")
    runtime.add_argument("--apk", type=Path, help="use the real runtime library inside this Android APK")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work-dir", type=Path, help="existing directory for temporary generated C and objects")
    parser.add_argument("--api", type=int, default=28)
    parser.add_argument("--jobs", type=int, default=2, help="compiler jobs (large generated units use substantial memory)")
    args = parser.parse_args()
    if args.api < 28 or args.jobs < 1 or args.jobs > 64:
        parser.error("--api must be >=28 and --jobs must be between 1 and 64")
    try:
        if args.apk:
            with tempfile.TemporaryDirectory(prefix='las-apk-runtime-') as temporary:
                args.runtime_lib = runtime_from_apk(args.apk, Path(temporary))
                print(build(args))
        else:
            print(build(args))
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, f"module build failed: {error}\n")


if __name__ == "__main__":
    main()
