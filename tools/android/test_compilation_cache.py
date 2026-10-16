import json
from pathlib import Path
import tempfile
import unittest
from compilation_cache import cache_ready, file_sha256, inputs, publish


class CompilationCacheTests(unittest.TestCase):
    def test_sha256_and_input_abi_output_invalidation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'exefs').mkdir()
            (root / 'exefs/main').write_bytes(b'abc')
            (root / 'exefs/sdk').write_bytes(b'sdk fixture')
            self.assertEqual(file_sha256(root / 'exefs/main'),
                             'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            original = inputs(root, 'fixture-id')
            module = root / 'staged.so'
            header = bytearray(64)
            header[:7] = b'\x7fELF\x02\x01\x01'
            header[16:20] = b'\x03\x00\xb7\x00'
            module.write_bytes(header)
            self.assertFalse(cache_ready(root, 'fixture-id'))
            publish(root, module, 'fixture-id', original)
            self.assertTrue(cache_ready(root, 'fixture-id'))
            self.assertFalse(cache_ready(root, 'other-abi'))
            (root / 'exefs/main').write_bytes(b'changed NSO')
            self.assertFalse(cache_ready(root, 'fixture-id'))
            (root / 'exefs/main').write_bytes(b'abc')
            (root / 'lasgame.so').chmod(0o600)
            (root / 'lasgame.so').write_bytes(header + b'changed module')
            self.assertFalse(cache_ready(root, 'fixture-id'))

    def test_input_change_preserves_previous_module(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'exefs').mkdir()
            (root / 'exefs/main').write_bytes(b'main')
            (root / 'exefs/sdk').write_bytes(b'sdk')
            original = inputs(root, 'id')
            (root / 'lasgame.so').write_bytes(b'previous module')
            (root / 'staged.so').write_bytes(b'not executable fixture')
            (root / 'exefs/sdk').write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError, 'changed during'):
                publish(root, root / 'staged.so', 'id', original)
            self.assertEqual((root / 'lasgame.so').read_bytes(), b'previous module')

    def test_interrupted_marker_and_wrong_architecture_are_misses(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'lasgame.cache').write_text('{')
            self.assertFalse(cache_ready(root, 'id'))
            (root / 'exefs').mkdir()
            (root / 'exefs/main').write_bytes(b'main')
            (root / 'exefs/sdk').write_bytes(b'sdk')
            (root / 'staged.so').write_bytes(b'not an ARM64 shared module')
            with self.assertRaisesRegex(RuntimeError, 'ARM64 Android'):
                publish(root, root / 'staged.so', 'id', inputs(root, 'id'))
            self.assertFalse((root / 'lasgame.so').exists())


if __name__ == '__main__':
    unittest.main()
