#!/usr/bin/env python3
"""Real loader, kernel and dispatch integration with tiny original NSOs (Linux)."""
import os
import argparse
import pathlib
import shutil
import signal
import struct
import subprocess
import tempfile
from address_layout import CANONICAL_GUEST_BASE, guest_base

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--guest-base', type=guest_base, default=CANONICAL_GUEST_BASE)
args = parser.parse_args()
OUT = ROOT / 'build/runtime_boot_test' / f'{args.guest_base:x}'
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get('CC') or shutil.which('clang')
if not CC:
    raise SystemExit('clang is required (the runtime uses musttail); set CC to its path')
subprocess.run([CC, '-D_GNU_SOURCE', '-Wno-macro-redefined', '-std=c11', '-O1', '-g', '-Wno-format', '-Iruntime',
                f'-DLARECOMP_GUEST_BASE={args.guest_base:#x}ull',
                '-pthread', 'tools/test_runtime_boot.c', 'runtime/loader.c', 'runtime/dispatch.c',
                'runtime/kernel.c', 'runtime/platform/android.c', '-ldl', '-o', str(OUT / 'boot')],
               cwd=ROOT, check=True)

def patch(b, off, value):
    b = bytearray(b)
    struct.pack_into('<I', b, off, value)
    return b

def nso(sdk=False):
    image = bytearray(0x3000)
    struct.pack_into('<I', image, 4, 0x100)
    image[0x100:0x104] = b'MOD0'
    struct.pack_into('<i', image, 0x104, 0x1100 - 0x100)
    names = b'\0fixture_sdk\0fixture_hle\0fixture_weak\0'
    image[0x1500:0x1500 + len(names)] = names
    syms = [(0, 0, 0, 0), (1, 0x12, 1 if sdk else 0, 0x80 if sdk else 0),
            (13, 0x12, 0, 0), (25, 0x22, 0, 0)]
    for i, (name, info, section, value) in enumerate(syms):
        struct.pack_into('<IBBHQQ', image, 0x1400 + 24 * i, name, info, 0, section, value, 4)
    struct.pack_into('<II', image, 0x1300, 1, len(syms))
    dyn = [(4, 0x1300), (5, 0x1500), (6, 0x1400), (10, len(names))]
    if not sdk:
        dyn += [(7, 0x1600), (8, 4 * 24)]
        for i, (typ, sym, add) in enumerate([(1026, 1, 0), (1026, 2, 0), (1026, 3, 0), (1027, 0, 0x80)]):
            struct.pack_into('<QQq', image, 0x1600 + 24 * i, 0x2000 + 8 * i, typ | sym << 32, add)
    dyn += [(0, 0)]
    for i, (tag, val) in enumerate(dyn):
        struct.pack_into('<QQ', image, 0x1100 + 16 * i, tag, val)
    raw = bytearray(0x100)
    raw[:4] = b'NSO0'
    for i in range(3):
        struct.pack_into('<III', raw, 0x10 + 16 * i, 0x100 + 0x1000 * i, 0x1000 * i, 0x1000)
        struct.pack_into('<I', raw, 0x60 + 4 * i, 0x1000)
    return raw + image

with tempfile.TemporaryDirectory(prefix='las-boot-') as tmp:
    tmp = pathlib.Path(tmp)
    valid = tmp / 'valid'
    valid.mkdir()
    (valid / 'main').write_bytes(nso())
    (valid / 'sdk').write_bytes(nso(True))
    cases = [('boot and three resets', valid, 'boot', None),
             ('empty compiled SDK table', valid, 'empty-table', None),
             ('oversized main stack', valid, 'bad-stack', 'invalid main stack'),
             ('zero main stack', valid, 'zero-stack', 'invalid main stack'),
             ('fatal from guest thread', valid, 'fatal-guest', 'brk #0'),
             ('signal from guest thread', valid, 'signal-guest', 'SIGILL accessing')]
    if args.guest_base != CANONICAL_GUEST_BASE:
        cases.append(('boot with canonical address occupied', valid, 'foreign-canonical', None))
    for name, mutate, message in [
        ('short-header', lambda b: b[:4], 'NSO header'),
        ('file-segment', lambda b: patch(b, 0x10, 0xfffffff0), 'segment'),
        ('image-limit', lambda b: patch(b, 0x3c, 0x80000000), 'module exceeds'),
        ('bad-mod0', lambda b: patch(b, 0x104, 0x7fffffff), 'MOD0'),
        ('bad-dynamic', lambda b: patch(b, 0x204, 0x7fffffff), 'dynamic'),
        ('bad-symbol', lambda b: patch(b, 0x1500 + 24, 0xfffffff0), 'symbol name'),
        ('bad-relocation', lambda b: patch(b, 0x1700, 0xfffffff0), 'relocation target'),
    ]:
        directory = tmp / name
        directory.mkdir()
        (directory / 'main').write_bytes(mutate(nso()))
        (directory / 'sdk').write_bytes(nso(True))
        cases.append((name, directory, 'load-only', message))
    failed = []
    for name, directory, mode, message in cases:
        with subprocess.Popen([str(OUT / 'boot'), str(directory), mode],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as p:
            try:
                stdout, stderr = p.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                # Read before SIGKILL: distinguish a runtime deadlock from the
                # kernel/runner collecting a very large expected-abort core.
                print('TIMEOUT ' + name, flush=True)
                for entry in ('status', 'wchan'):
                    try:
                        print(entry + ':\n' + pathlib.Path(f'/proc/{p.pid}/{entry}').read_text(), flush=True)
                    except OSError as error:
                        print(str(error), flush=True)
                p.kill()
                stdout, stderr = p.communicate()
                print(stderr[-6000:], flush=True)
                raise
        ok = p.returncode == 0 if message is None else p.returncode == -signal.SIGABRT and message in stderr
        print(('PASS ' if ok else 'FAIL ') + name, flush=True)
        if not ok:
            failed.append(name)
            print('exit status:', p.returncode)
            print(stderr[-3000:])
    if failed:
        raise SystemExit('Failed: ' + ', '.join(failed))
