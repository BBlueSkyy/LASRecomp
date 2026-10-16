#!/usr/bin/env python3
"""Stage the opt-in APK payload from already built, public dependencies."""
import argparse
from pathlib import Path
import shutil


def stage(root):
    deps = root / 'android/deps'
    python = deps / 'arm64-v8a/python'
    compiler = deps / 'compiler-assets'
    for path in (python / 'assets/python/manifest.json', compiler / 'manifest.json',
                 python / 'lib/libpython3.14.so', python / 'lib/libunicorn.so',
                 python / 'lib/libcapstone.so'):
        if not path.is_file():
            raise ValueError('Required embedded dependency is missing: ' + str(path))
    assets = deps / 'embedded-assets/embedded'
    for name, source in (('python', python / 'assets/python'), ('compiler', compiler)):
        destination = assets / name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
    libraries = deps / 'setup-jni/arm64-v8a'
    libraries.mkdir(parents=True, exist_ok=True)
    for old in libraries.glob('*.so'):
        old.unlink()
    for library in (python / 'lib').glob('*.so'):
        if library.name != 'libpython3.14.so':  # CMake imported target packages this one.
            shutil.copyfile(library, libraries / library.name)
    return assets


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    print(stage(parser.parse_args().root.resolve()))
