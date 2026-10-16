#!/usr/bin/env python3
"""Compile real native fixture modules and exercise the loader without game data."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from gamelib_id import gamelib_id, SOURCES
from address_layout import CANONICAL_GUEST_BASE, guest_base

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/android"


class ModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="las-module-tests-")
        cls.work = Path(cls.temp.name)
        # cpu.h uses Clang vector/bitreverse builtins, just like the real build.
        cls.cc = os.environ.get("CC", "clang")
        cls.base = guest_base(os.environ.get("LARECOMP_TEST_GUEST_BASE", CANONICAL_GUEST_BASE))
        cls.flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "runtime"),
                     f'-DGAMELIB_ID="{gamelib_id(ROOT, cls.base)}"', f'-DLARECOMP_GUEST_BASE={cls.base:#x}ull']

        def compile(*args):
            subprocess.run([cls.cc, *cls.flags, *map(str, args)], check=True)

        cls.runtime = cls.work / "libfixture_runtime.so"
        compile("-fPIC", "-shared", ROOT / "runtime/gamelib.c", ROOT / "runtime/gamelib_validate.c",
                TOOLS / "test_runtime_fixture.c", "-Wl,-z,global", "-Wl,-soname,libfixture_runtime.so", "-ldl", "-o", cls.runtime)
        cls.driver = cls.work / "driver"
        compile(TOOLS / "test_gamelib.c", ROOT / "runtime/gamelib_validate.c", "-ldl", "-o", cls.driver)
        cls.modules = []
        for kind in range(9):
            output = cls.work / f"module-{kind}.so"
            compile("-fPIC", "-shared", f"-DBAD_KIND={kind}", TOOLS / "test_module_fixture.c",
                    cls.runtime, *([] if kind == 8 else ["-Wl,--no-undefined"]),
                    "-Wl,-rpath,$ORIGIN", "-o", output)
            cls.modules.append(output)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_driver(self, *args):
        subprocess.run([str(self.driver), *map(str, args)], check=True)

    def test_invalid_tables_then_real_import_and_retry(self):
        for bad in self.modules[1:]:
            with self.subTest(module=bad.name):
                self.run_driver("load", self.runtime, bad, self.modules[0])

    def test_elf_before_load(self):
        self.run_driver("elf", self.modules[0], 1)
        original = self.modules[0].read_bytes()
        variants = {"truncated": original[:31], "non-elf": b"hello"}
        wrong_cpu = bytearray(original)
        struct.pack_into("<H", wrong_cpu, 18, 40)  # EM_ARM, not the host's 64-bit ISA
        variants["wrong-cpu"] = wrong_cpu
        missing_headers = bytearray(original)
        struct.pack_into("<Q", missing_headers, 32, len(original) + 4096)
        variants["outside-phdr"] = missing_headers
        for name, data in variants.items():
            path = self.work / name
            path.write_bytes(data)
            self.run_driver("elf", path, 0)

    def test_id_tracks_every_abi_input(self):
        copied = self.work / "inputs"
        for name in SOURCES:
            destination = copied / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        initial = gamelib_id(copied, self.base)
        self.assertEqual(initial, gamelib_id(ROOT, self.base))
        for name in SOURCES:
            path = copied / name
            content = path.read_bytes()
            path.write_bytes(content + b"\n")
            self.assertNotEqual(initial, gamelib_id(copied, self.base), name)
            path.write_bytes(content)


if __name__ == "__main__":
    unittest.main()
