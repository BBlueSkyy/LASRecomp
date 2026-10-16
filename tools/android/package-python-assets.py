#!/usr/bin/env python3
"""Stage pinned embedded Python, bindings, source tools and their licenses.

Native code goes into jniLibs via python/lib, never writable extracted assets.
Run again after generator source changes; it does not rebuild dependencies.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def package(root):
    src = root / 'android/deps/src'
    output = root / 'android/deps/arm64-v8a/python'
    assets = output / 'assets/python'
    lib = output / 'lib'
    cpython = src / 'cpython-3.14.8/prefix'
    lib.mkdir(parents=True, exist_ok=True)
    shutil.copytree(cpython / 'include', output / 'include', dirs_exist_ok=True)
    if assets.exists():
        shutil.rmtree(assets)
    stdlib = assets / 'python3.14'
    shutil.copytree(cpython / 'lib/python3.14', stdlib,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.so', 'test', 'tests', 'idlelib', 'tkinter', 'ensurepip', 'turtledemo'))
    for filename in (cpython / 'lib').glob('*.so'):
        # Preserve licenses for packaged libraries; exclude unneeded SSL/sqlite libs.
        # Preserve dependency closure of the packaged stdlib extensions, including libffi.
        shutil.copyfile(filename, lib / filename.name)
    # Required stdlib extensions only: dependency closure of the actual generator
    # plus common encodings/hash algorithms. All load from nativeLibraryDir.
    extensions = ('_struct', '_ctypes', 'math', 'binascii', 'zlib', '_bz2', '_lzma', '_opcode',
                  '_json', '_bisect', '_heapq', '_typing', '_random', '_sha2', '_sha3', '_blake2',
                  '_hashlib', '_posixsubprocess', 'select', '_socket', 'array', 'unicodedata', 'fcntl')
    for name in extensions:
        matches = list((cpython / 'lib/python3.14/lib-dynload').glob(name + '.*.so'))
        if matches and name != '_hashlib':  # hashlib has native SHA implementations, no OpenSSL required
            shutil.copyfile(matches[0], lib / f'libpyext_{name}.so')
    tools = assets / 'tools'
    tools.mkdir(parents=True)
    for filename in (root / 'tools').glob('*.py'):
        if not filename.name.startswith('test_'):
            shutil.copyfile(filename, tools / filename.name)
    for name in ('embedded_generate.py', 'compilation_cache.py'):
        shutil.copyfile(root / 'tools/android' / name, tools / name)
    (tools / 'rando').mkdir()
    shutil.copyfile(root / 'tools/rando/patch_variants.json', tools / 'rando/patch_variants.json')
    shutil.copyfile(root / 'runtime/hle/hle_symbols.txt', tools / 'hle_symbols.txt')
    for name, version in (('unicorn', '2.1.4'), ('capstone', '5.0.9')):
        shutil.copytree(src / f'{name}-{version}' / name, tools / name,
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.so', '*.dll', '*.dylib', '*.a', 'lib'))
    licenses = assets / 'licenses'
    licenses.mkdir()
    # CPython's installed LICENSE.txt contains the PSF and bundled third-party notices.
    shutil.copyfile(cpython / 'lib/python3.14/LICENSE.txt', licenses / 'Python-LICENSE.txt')
    shutil.copyfile(src / 'capstone-5.0.9/LICENSE.TXT', licenses / 'Capstone-LICENSE.txt')
    for item in (src / 'unicorn-2.1.4/src').glob('COPYING*'):
        shutil.copyfile(item, licenses / ('Unicorn-' + item.name))
    manifest = {'python': '3.14.8', 'unicorn': '2.1.4', 'capstone': '5.0.9',
                'lz4': 'LZ4 block specification, pure Python decoder',
                'sources': {'python': 'https://www.python.org/ftp/python/3.14.8/',
                            'unicorn': 'https://github.com/unicorn-engine/unicorn/tree/2.1.4',
                            'capstone': 'https://github.com/capstone-engine/capstone/tree/5.0.9'},
                'sha256': {}}
    for base in (assets, lib):
        for filename in sorted(base.rglob('*')):
            if filename.is_file():
                manifest['sha256'][str(filename.relative_to(output))] = hashlib.sha256(filename.read_bytes()).hexdigest()
    (assets / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return assets


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    print(package(parser.parse_args().root))
