"""On-device static build orchestration. Native callbacks execute Clang and LLD.

No subprocess or external compiler is used here. Only the caller's imported
NSOs and generated private artifacts are read; none are packaged with the app.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import tempfile


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def inputs(game, identity):
    return {'abi': identity, 'main': file_sha256(game / 'exefs/main'),
            'sdk': file_sha256(game / 'exefs/sdk')}


def android_module(path):
    with open(path, 'rb') as stream:
        header = stream.read(64)
    return (len(header) == 64 and header[:7] == b'\x7fELF\x02\x01\x01'
            and header[16:20] == b'\x03\x00\xb7\x00')


def cache_ready(game, identity):
    game = Path(game)
    try:
        stamp = json.loads((game / 'lasgame.cache').read_text())
        return (stamp['inputs'] == inputs(game, identity)
                and (game / 'lasgame.so.id').read_text().strip() == identity
                and android_module(game / 'lasgame.so')
                and stamp['module'] == file_sha256(game / 'lasgame.so'))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def atomic_text(path, content):
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def publish(game, module, identity, original_inputs):
    """The cache marker is published last; interrupted replacement is never a hit."""
    game, module = Path(game), Path(module)
    if inputs(game, identity) != original_inputs:
        raise RuntimeError('Imported game files changed during compilation; previous module retained')
    if not android_module(module):
        raise RuntimeError('Embedded linker did not produce an ARM64 Android shared module')
    output_hash = file_sha256(module)
    os.chmod(module, 0o444)
    os.replace(module, game / 'lasgame.so')
    atomic_text(game / 'lasgame.so.id', identity + '\n')
    atomic_text(game / 'lasgame.cache', json.dumps({'inputs': original_inputs, 'module': output_hash}) + '\n')
    directory = os.open(game, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def build_cached(game_dir, compiler_assets, identity, compile_one, link, progress=None):
    """Compile actual original-generator output through native in-process callbacks."""
    import fcntl
    from embedded_generate import generate
    game = Path(game_dir)
    report = progress or (lambda message: None)
    with (game / '.las-build.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('Game preparation is already running') from error
        original = inputs(game, identity)
        if cache_ready(game, identity):
            report('Reusing the verified compiled game module')
            return
        with tempfile.TemporaryDirectory(prefix='.las-build-', dir=game) as work:
            work = Path(work)
            report('Generating ARM64 C from the imported NSOs')
            generate(str(game), str(work), report)
            sources = [Path(compiler_assets) / 'gamelib_entry.c']
            for module in ('main', 'sdk'):
                sources += sorted((work / module).glob('*.c'))
            if len(sources) < 5 or not all(source.is_file() for source in sources):
                raise RuntimeError('Static generator output is incomplete')
            objects = []
            for index, source in enumerate(sources):
                obj = work / ('unit-%05d.o' % index)
                report('Compiling game code %d/%d' % (index + 1, len(sources)))
                compile_one(str(source), str(obj))
                if not obj.is_file() or not obj.stat().st_size:
                    raise RuntimeError('Embedded Clang produced no object: ' + source.name)
                objects.append(str(obj))
            module = work / 'lasgame.so'
            report('Linking the compiled ARM64 game module')
            link(objects, str(module))
            publish(game, module, identity, original)
            report('Compiled module published; game execution has not started yet')
