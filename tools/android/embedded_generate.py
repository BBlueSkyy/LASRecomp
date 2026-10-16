"""In-process Android entry to the original NSO static generator.

Only public source code is packaged. Game files always come from the owner's
private extracted exefs directory. No subprocess, pip, or executable is used.
"""
import sys


def install_native_loader(directory):
    """Load CPython extensions from Android's read-only native library directory.

    Android only extracts lib*.so entries; preserve PyInit_<name> by keeping the
    original module name in ExtensionFileLoader, independently of the filename.
    """
    import importlib.util
    import importlib.machinery

    class NativeExtensions:
        def find_spec(self, fullname, path=None, target=None):
            name = fullname.rsplit('.', 1)[-1]
            filename = directory + '/libpyext_' + name + '.so'
            try:
                with open(filename, 'rb'):
                    pass
            except OSError:
                return None
            loader = importlib.machinery.ExtensionFileLoader(fullname, filename)
            return importlib.util.spec_from_file_location(fullname, filename, loader=loader)

    sys.meta_path.insert(0, NativeExtensions())
    # The upstream ctypes bindings support these explicit library search paths.
    import os
    os.environ['LIBUNICORN_PATH'] = directory
    os.environ['LIBCAPSTONE_PATH'] = directory


class LZ4BlockError(ValueError):
    pass


def decompress_block(source, uncompressed_size=-1, return_bytearray=False):
    """Decode the standard LZ4 block format, including overlapping matches.

    NSO specifies a separate decompressed size. The optional size-prefix form
    is supported as well, for parity with the desktop lz4.block API. This pure
    Python implementation avoids executing an extension from writable assets.
    """
    source = memoryview(source).cast('B')
    size = len(source)
    pos = 0
    if uncompressed_size < 0:
        if size < 4:
            raise LZ4BlockError('LZ4 block is missing its size prefix')
        uncompressed_size = int.from_bytes(source[:4], 'little')
        pos = 4
    if uncompressed_size > 0x7fffffff:
        raise LZ4BlockError('LZ4 output size exceeds the block format limit')
    output = bytearray()

    def length(initial):
        nonlocal pos
        value = initial
        if initial == 15:
            while True:
                if pos >= size:
                    raise LZ4BlockError('truncated LZ4 length')
                extra = source[pos]
                pos += 1
                value += extra
                if extra != 255:
                    break
        return value

    while pos < size:
        token = source[pos]
        pos += 1
        literals = length(token >> 4)
        if pos + literals > size or len(output) + literals > uncompressed_size:
            raise LZ4BlockError('invalid LZ4 literal run')
        output.extend(source[pos:pos + literals])
        pos += literals
        if pos == size:
            break
        if pos + 2 > size:
            raise LZ4BlockError('truncated LZ4 match offset')
        offset = source[pos] | source[pos + 1] << 8
        pos += 2
        if not offset or offset > len(output):
            raise LZ4BlockError('invalid LZ4 match offset')
        match = length(token & 15) + 4
        if len(output) + match > uncompressed_size:
            raise LZ4BlockError('LZ4 match exceeds output size')
        # Doubling copies preserve overlap semantics without a Python loop per byte.
        while match:
            count = min(match, offset)
            output.extend(output[-offset:][:count])
            match -= count
            offset += count
    return output if return_bytearray else bytes(output)


def install_lz4():
    import types
    package = types.ModuleType('lz4')
    package.__path__ = []
    block = types.ModuleType('lz4.block')
    block.decompress = decompress_block
    block.LZ4BlockError = LZ4BlockError
    package.block = block
    sys.modules['lz4'] = package
    sys.modules['lz4.block'] = block


def generate(game_dir, output_dir, progress=None):
    """Generate both modules synchronously; raise the original error on failure."""
    import contextlib
    import io
    import os
    import runpy
    from pathlib import Path

    install_lz4()
    tools = Path(__file__).resolve().parent
    game = Path(game_dir)
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    for name in ('main', 'sdk'):
        with (game / 'exefs' / name).open('rb') as stream:
            if stream.read(4) != b'NSO0':
                raise ValueError(f'{name}: the imported file is not an NSO module')

    class Progress(io.TextIOBase):
        def __init__(self):
            self.pending = ''

        def write(self, text):
            self.pending += text
            while '\n' in self.pending:
                line, self.pending = self.pending.split('\n', 1)
                if progress and line:
                    progress(line)
            return len(text)

        def flush(self):
            if self.pending and progress:
                progress(self.pending)
            self.pending = ''

    # Native modules are genuine upstream Unicorn and Capstone, prebuilt for ARM64.
    import unicorn
    import capstone
    # Exercise both ctypes libraries before touching potentially large NSOs.
    mu = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
    mu.mem_map(0x1000, 0x1000)
    mu.mem_write(0x1000, bytes.fromhex('200080d2c0035fd6'))  # mov x0,#1; ret
    mu.emu_start(0x1000, 0x1004, count=1)
    if not list(capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM).disasm(bytes.fromhex('c0035fd6'), 0)):
        raise RuntimeError('Capstone ARM64 decoder self-check failed')
    del mu
    original_argv = sys.argv
    sink = Progress()
    try:
        with contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            for name, base in (('main', '0x7000000000'), ('sdk', '0x7004000000')):
                sys.argv = [str(tools / 'gen.py'), name, str(game / 'exefs' / name), base,
                            str(output / name), '--simd', 'asm']
                if name == 'sdk':
                    sys.argv += ['--hle', str(tools / 'hle_symbols.txt')]
                runpy.run_path(str(tools / 'gen.py'), run_name='__main__')
                if not (output / name / f'{name}_table.c').is_file() or not list((output / name).glob(f'{name}_[0-9]*.c')):
                    raise RuntimeError(f'{name}: generator produced no static functions')
            sink.flush()
    finally:
        sys.argv = original_argv
