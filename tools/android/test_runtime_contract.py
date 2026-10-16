"""Read the runtime build ABI from public synthetic ELF fixtures; no guest execution."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest
import build_module as builder
from gamelib_id import gamelib_id
from address_layout import ANDROID_GUEST_BASE

ROOT = Path(__file__).resolve().parents[2]


class RuntimeContractTests(unittest.TestCase):
    def fixture(self, work, identity=None, base=ANDROID_GUEST_BASE, symbol=True):
        identity = identity or gamelib_id(ROOT, base)
        source = work / 'contract.c'
        source.write_text('''#include <stdint.h>
struct Contract { char magic[4]; uint32_t format, abi, size; uint64_t base; char id[17]; };
const struct Contract %s = {{'L','A','S','C'},1,1,sizeof(struct Contract),%s,"%s"};
''' % ('lasrecomp_build_contract_v1' if symbol else 'another_symbol', hex(base), identity))
        library = work / 'runtime.so'
        subprocess.run([os.environ.get('HOST_CC', 'cc'), '-shared', '-fPIC', str(source), '-o', str(library)], check=True)
        data = bytearray(library.read_bytes())
        # Only metadata is read: this does not make host instructions ARM64 code.
        struct.pack_into('<H', data, 18, 183)
        library.write_bytes(data)
        return library

    def require_reader(self):
        self.assertTrue(callable(getattr(builder, 'runtime_contract', None)),
                        'module builder must read the contract of the actual APK runtime')

    def test_actual_runtime_layout_and_identity_are_read(self):
        self.require_reader()
        with tempfile.TemporaryDirectory() as tmp:
            library = self.fixture(Path(tmp), base=0x2000000000)
            contract = builder.runtime_contract(library)
            self.assertEqual(contract['guest_base'], 0x2000000000)
            self.assertEqual(contract['id'], gamelib_id(ROOT, 0x2000000000))
            self.assertEqual(contract['abi'], 1)

    def test_wrong_source_or_requested_layout_rejected_before_generation(self):
        self.require_reader()
        with tempfile.TemporaryDirectory() as tmp:
            library = self.fixture(Path(tmp))
            self.assertEqual(builder.match_runtime(library, ROOT, None),
                             (ANDROID_GUEST_BASE, gamelib_id(ROOT)))
            with self.assertRaisesRegex(ValueError, 'layout'):
                builder.match_runtime(library, ROOT, 0x2000000000)
            self.fixture(Path(tmp), identity='0000000000000000')
            with self.assertRaisesRegex(ValueError, 'generator|sources'):
                builder.match_runtime(library, ROOT, None)

    def test_old_or_malformed_runtime_is_rejected(self):
        self.require_reader()
        with tempfile.TemporaryDirectory() as tmp:
            library = self.fixture(Path(tmp), symbol=False)
            with self.assertRaisesRegex(ValueError, 'contract'):
                builder.runtime_contract(library)
            self.fixture(Path(tmp))
            data = bytearray(library.read_bytes())
            struct.pack_into('<Q', data, 40, len(data) + 1)  # section table outside file
            library.write_bytes(data)
            with self.assertRaises(ValueError):
                builder.runtime_contract(library)

    def test_exported_production_contract_has_the_same_binary_layout(self):
        self.require_reader()
        with tempfile.TemporaryDirectory() as tmp:
            library = Path(tmp) / 'runtime.so'
            identity = gamelib_id(ROOT)
            subprocess.run([os.environ.get('HOST_CC', 'cc'), '-shared', '-fPIC',
                            f'-DGAMELIB_ID="{identity}"', f'-DLARECOMP_GUEST_BASE={ANDROID_GUEST_BASE}',
                            '-I', str(ROOT / 'runtime'), str(ROOT / 'runtime/gamelib_contract.c'),
                            '-o', str(library)], check=True)
            data = bytearray(library.read_bytes())
            struct.pack_into('<H', data, 18, 183)
            library.write_bytes(data)
            self.assertEqual(builder.match_runtime(library, ROOT, None), (ANDROID_GUEST_BASE, identity))


if __name__ == '__main__':
    unittest.main()
