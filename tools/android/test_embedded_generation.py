"""Generate public synthetic NSOs in-process, compile and execute their lifted C.

This is NOT Nintendo game execution. It exercises the original static generator
and real runtime profiler/platform symbols without any proprietary input.
"""
import ctypes
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from compilation_cache import build_cached, cache_ready
from gamelib_id import COMPILE_FLAGS, LINK_FLAGS, gamelib_id

ROOT = Path(__file__).resolve().parents[2]


def original_nso():
    image = bytearray(0x3000)
    struct.pack_into('<I', image, 4, 0x1000)
    image[0x80:0x88] = bytes.fromhex('400580d2c0035fd6')  # mov x0,#42; ret
    image[0x1000:0x1004] = b'MOD0'
    struct.pack_into('<i', image, 0x1004, 0x100)  # dynamic table
    struct.pack_into('<i', image, 0x1010, -0x1000)  # absent EH frame table
    struct.pack_into('<i', image, 0x1014, -0x1000)
    names = b'\0public_fixture_entry\0'
    image[0x1500:0x1500 + len(names)] = names
    struct.pack_into('<IBBHQQ', image, 0x1418, 1, 0x12, 0, 1, 0x80, 8)
    struct.pack_into('<II', image, 0x1300, 1, 2)
    for i, pair in enumerate(((4, 0x1300), (5, 0x1500), (6, 0x1400), (10, len(names)), (0, 0))):
        struct.pack_into('<QQ', image, 0x1100 + 16 * i, *pair)
    raw = bytearray(0x100)
    raw[:4] = b'NSO0'
    segments = ((0x100, 0, 0x100), (0x200, 0x1000, 0x1000), (0x1200, 0x2000, 0x1000))
    for i, (offset, memory, size) in enumerate(segments):
        struct.pack_into('<III', raw, 0x10 + 16 * i, offset, memory, size)
        struct.pack_into('<I', raw, 0x60 + 4 * i, size)
    return raw + image[:0x100] + image[0x1000:]


class EmbeddedGeneratorTests(unittest.TestCase):
    def test_original_generator_and_lifted_function(self):
        cc = os.environ.get('CC') or shutil.which('clang')
        self.assertTrue(cc, 'Clang is required to compile the original runtime headers')
        with tempfile.TemporaryDirectory(prefix='las-generator-') as tmp:
            stage = Path(tmp)
            tools = stage / 'tools'
            tools.mkdir()
            for source in (ROOT / 'tools').glob('*.py'):
                shutil.copyfile(source, tools / source.name)
            for name in ('embedded_generate.py',):
                shutil.copyfile(ROOT / 'tools/android' / name, tools / name)
            shutil.copyfile(ROOT / 'runtime/hle/hle_symbols.txt', tools / 'hle_symbols.txt')
            (tools / 'rando').mkdir()
            shutil.copyfile(ROOT / 'tools/rando/patch_variants.json', tools / 'rando/patch_variants.json')
            game = stage / 'game'
            (game / 'exefs').mkdir(parents=True)
            for name in ('main', 'sdk'):
                (game / 'exefs' / name).write_bytes(original_nso())
            spec = importlib.util.spec_from_file_location('installed_generator', tools / 'embedded_generate.py')
            embedded = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(embedded)
            before = list(sys.path)
            sys.path.insert(0, str(tools))
            try:
                embedded.generate(str(game), str(stage / 'output'))
            finally:
                sys.path[:] = before
            source = stage / 'output/main/main_000.c'
            self.assertTrue(source.is_file())
            output = stage / 'lifted.so'
            subprocess.run([cc, '-shared', '-fPIC', '-O1', '-Wno-format', '-I', str(ROOT / 'runtime'),
                            str(source), str(ROOT / 'runtime/fnprof.c'), str(ROOT / 'runtime/platform/android.c'),
                            '-pthread', '-ldl', '-o', str(output)], check=True)
            library = ctypes.CDLL(str(output))
            function = library.f_7000000080
            function.argtypes = [ctypes.c_void_p]
            function.restype = None
            storage = ctypes.create_string_buffer(1040)
            address = (ctypes.addressof(storage) + 15) & ~15
            function(address)
            self.assertEqual(ctypes.c_uint64.from_address(address).value, 42)
            self.assertTrue((stage / 'output/sdk/sdk_table.c').is_file())
            # The same generated source must also compile for the actual APK ABI.
            ndk = os.environ.get('ANDROID_NDK_HOME')
            if ndk:
                target = Path(ndk) / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang'
                runtime = stage / 'liblasrecomp.so'
                # These are actual runtime sources, not import stubs. This fixture
                # exercises their ARM64 link boundary, not Android execution.
                subprocess.run([str(target), '-shared', '-fPIC', '-O1', '-I', str(ROOT / 'runtime'),
                                str(ROOT / 'runtime/fnprof.c'), str(ROOT / 'runtime/platform/android.c'),
                                '-Wl,-soname,liblasrecomp.so', '-llog', '-ldl', '-o', str(runtime)], check=True)
                identity = gamelib_id(ROOT)
                compiled = []

                def compile_one(unit, obj):
                    compiled.append(unit)
                    subprocess.run([str(target), *COMPILE_FLAGS, f'-DGAMELIB_ID="{identity}"',
                                    '-I', str(ROOT / 'runtime'), '-c', unit, '-o', obj], check=True)

                def link(objects, output):
                    subprocess.run([str(target), *LINK_FLAGS, *objects, str(runtime), '-lm',
                                    '-o', output], check=True)

                # The APK uses in-process callbacks. The host fixture deliberately
                # substitutes the NDK CLI to validate the same coordinator/output.
                previous = sys.modules.get('embedded_generate')
                sys.modules['embedded_generate'] = embedded
                sys.path.insert(0, str(tools))
                try:
                    build_cached(game, ROOT / 'tools', identity, compile_one, link)
                    self.assertTrue(cache_ready(game, identity))
                    self.assertGreaterEqual(len(compiled), 5)
                    count = len(compiled)
                    build_cached(game, ROOT / 'tools', identity, compile_one, link)
                    self.assertEqual(len(compiled), count, 'cache hit must skip generation and compilation')
                    self.assertFalse(list(game.glob('.las-build-*')))
                finally:
                    sys.path.pop(0)
                    if previous is None:
                        sys.modules.pop('embedded_generate', None)
                    else:
                        sys.modules['embedded_generate'] = previous


if __name__ == '__main__':
    unittest.main()
