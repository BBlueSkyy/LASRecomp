#!/usr/bin/env python3
"""Relocated identity-address generation using only original public fixtures."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/android'))
from test_embedded_generation import original_nso
from gen import Module

SHIFTED = 0x1000000000
CANONICAL = 0x7000000000


class GuestLayoutTests(unittest.TestCase):
    def test_shifted_patch_keeps_data_lr_function_and_label_addresses(self):
        module = Module.__new__(Module)
        module.name, module.base = 'main', SHIFTED
        image = bytearray(0x109fcac)
        # Missing-string replacement emits both a data pointer and local branch.
        struct.pack_into('<I', image, 0x109fca8, 0x321c03e0)
        module.nso = types.SimpleNamespace(image=image)
        self.assertTrue(hasattr(module, 'patch_at'), 'Module needs layout-aware patch lookup')
        code = module.patch_at(SHIFTED + 0x109fca8)
        self.assertIn('0x10016cf23aull', code)
        self.assertIn('goto L_100109fc78', code)
        self.assertNotIn('7001', code)
        struct.pack_into('<I', image, 0x8c45b8, 0x9400351e)
        code = module.patch_at(SHIFTED + 0x8c45b8)
        self.assertIn('f_10008d1a30(c)', code)
        self.assertIn('0x10008c45bcull', code)
        module.base = CANONICAL
        self.assertIn('f_70008d1a30(c)', module.patch_at(CANONICAL + 0x8c45b8))
        self.assertIsNone(module.patch_at(CANONICAL + 0x81))

    def test_original_generator_executes_shifted_adrp_and_literal_load(self):
        cc = os.environ.get('CC') or shutil.which('clang')
        self.assertTrue(cc, 'Clang is required')
        with tempfile.TemporaryDirectory(prefix='las-layout-') as directory:
            work = Path(directory)
            raw = bytearray(original_nso())
            words = (0x90000001, 0x91400821, 0xf9400020, 0x58000062, 0xd2c00e03, 0xd65f03c0)
            for i, word in enumerate(words):
                struct.pack_into('<I', raw, 0x180 + 4 * i, word)
            struct.pack_into('<Q', raw, 0x198, 42)
            nso = work / 'main'
            nso.write_bytes(raw)
            out = work / 'generated'
            subprocess.run([sys.executable, str(ROOT / 'tools/gen.py'), 'main', str(nso), hex(SHIFTED),
                            str(out), '--simd', 'asm'], check=True, capture_output=True, text=True)
            generated = (out / 'main_000.c').read_text()
            self.assertIn('0x1000000000ull', generated)
            # MOVZ's ordinary integer value must retain all its original bits.
            self.assertIn('0x7000000000ull', generated)
            driver = work / 'driver.c'
            driver.write_text('''#define _GNU_SOURCE
#include "cpu.h"
#include <assert.h>
#include <sys/mman.h>
volatile int g_fnprof;
void fnprof_hit(uint64_t a,uint64_t b,uint64_t d){(void)a;(void)b;(void)d;}
void rt_udf(Cpu *c,uint64_t p){(void)c;(void)p;__builtin_trap();}
extern void f_1000000080(Cpu *);
int main(void){
 void *p=mmap((void *)0x1000000000ull,0x3000,PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(p==(void *)0x1000000000ull);
 *(uint64_t *)0x1000002000ull=0xbeef;
 *(uint64_t *)0x1000000098ull=42;
 Cpu c={0}; f_1000000080(&c);
 assert(c.x[0]==0xbeef && c.x[1]==0x1000002000ull && c.x[2]==42);
 assert(c.x[3]==0x7000000000ull);
 return 0;
}
''')
            exe = work / 'run'
            subprocess.run([cc, '-O1', '-I', str(ROOT / 'runtime'), str(driver),
                            str(out / 'main_000.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_shifted_patch_executes_calls_and_missing_string(self):
        cc = os.environ.get('CC') or shutil.which('clang')
        self.assertTrue(cc, 'Clang is required')
        module = Module.__new__(Module)
        module.name, module.base = 'main', SHIFTED
        image = bytearray(0x109fcac)
        struct.pack_into('<I', image, 0x109fca8, 0x321c03e0)
        struct.pack_into('<I', image, 0x8c45b8, 0x9400351e)
        module.nso = types.SimpleNamespace(image=image)
        self.assertTrue(hasattr(module, 'patch_at'), 'Module needs layout-aware patch lookup')
        string_patch = module.patch_at(SHIFTED + 0x109fca8)
        owned_patch = module.patch_at(SHIFTED + 0x8c45b8)
        source = ('#include "cpu.h"\n#include <assert.h>\n'
                  'int rt_underwater_skip_owned(uint64_t a){(void)a;return 0;}\n'
                  'void f_10008d1a30(Cpu *c){assert(c->x[30]==0x10008c45bcull);c->x[0]=77;}\n'
                  'static void missing(Cpu *c){' + string_patch + '\nL_100109fc78: return;}\n'
                  'static void owned(Cpu *c){' + owned_patch + '}\n'
                  'int main(void){uint64_t stack[8]={0};Cpu c={0};c.sp=(uintptr_t)stack;'
                  'missing(&c);assert(stack[3]==0x10016cf23aull && stack[4]==0);'
                  'owned(&c);assert(c.x[0]==77);return 0;}\n')
        with tempfile.TemporaryDirectory(prefix='las-patch-layout-') as directory:
            work = Path(directory)
            (work / 'patch.c').write_text(source)
            subprocess.run([cc, '-O1', '-I', str(ROOT / 'runtime'), str(work / 'patch.c'),
                            '-o', str(work / 'patch')], check=True)
            subprocess.run([str(work / 'patch')], check=True)

    def test_layout_fingerprint_and_range_validation(self):
        from gamelib_id import gamelib_id
        self.assertNotEqual(gamelib_id(ROOT, CANONICAL), gamelib_id(ROOT, SHIFTED))
        for base in (0, 0x1000000001, 0x7d00000000, -0x4000):
            with self.subTest(base=base), self.assertRaises(ValueError):
                gamelib_id(ROOT, base)


if __name__ == '__main__':
    unittest.main()
