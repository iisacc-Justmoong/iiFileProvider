"""File creation owned by iiFileProvider; existing Python codecs borrow streams.

The adapter loads the native library. Missing native support is an error, never
a fallback to an independent final-file writer. No model or GUI dependency.
"""
from __future__ import annotations

import builtins
import contextlib
import ctypes
import errno
import io
import os
from pathlib import Path
import shutil
import sys
import tempfile as _tempfile
import uuid

_library = None


def _native():
    global _library
    if _library is not None:
        return _library
    name = "iiFileProviderNative.dll" if os.name == "nt" else (
        "libiiFileProviderNative.dylib" if sys.platform == "darwin" else "libiiFileProviderNative.so")
    candidates = []
    if os.environ.get("IIFILEPROVIDER_NATIVE_LIBRARY"):
        candidates.append(Path(os.environ["IIFILEPROVIDER_NATIVE_LIBRARY"]))
    # Installed share/iiFileProvider/python and source python layouts.
    for parent in Path(__file__).resolve().parents:
        candidates.extend((parent / "lib" / name, parent / "bin" / name, parent / name))
        if sys.platform == "darwin":
            candidates.extend(sorted((parent / "lib").glob("libiiFileProviderNative*.dylib")))
            candidates.extend(sorted((parent / "Frameworks").glob("libiiFileProviderNative*.dylib")))
        elif os.name != "nt":
            candidates.extend(sorted((parent / "lib").glob("libiiFileProviderNative.so.*")))
    prefix = Path.home() / ".local" / "SDK" / "iiFileProvider"
    candidates.extend((prefix / "lib" / name, prefix / "bin" / name))
    for candidate in candidates:
        if candidate.is_file():
            library = ctypes.CDLL(str(candidate), use_errno=True)
            if not hasattr(library, "iifp_present_later"):
                continue
            library.iifp_last_error.restype = ctypes.c_char_p
            library.iifp_last_errno.restype = ctypes.c_int
            library.iifp_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
            library.iifp_mkdir.argtypes = [ctypes.c_char_p, ctypes.c_int]
            library.iifp_copy.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
            library.iifp_publish.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
            library.iifp_stage.argtypes = [ctypes.c_char_p]
            library.iifp_stage.restype = ctypes.c_void_p
            library.iifp_stage_path.argtypes = [ctypes.c_void_p]
            library.iifp_stage_path.restype = ctypes.c_char_p
            library.iifp_stage_publish.argtypes = [ctypes.c_void_p, ctypes.c_int]
            library.iifp_stage_discard.argtypes = [ctypes.c_void_p]
            library.iifp_present_later.argtypes = [ctypes.c_char_p]
            library.iifp_present_later.restype = None
            library.iifp_present.argtypes = [ctypes.c_char_p]
            library.iifp_presentation.argtypes = [ctypes.c_char_p]
            library.iifp_presentation.restype = ctypes.c_char_p
            library.iifp_presentation_wait.argtypes = [ctypes.c_int]
            library.iifp_presentation_cancel.argtypes = [ctypes.c_char_p]
            library.iifp_presentation_cancel.restype = None
            if hasattr(library, "iifp_cache_change_token"):
                library.iifp_cache_change_token.argtypes = [ctypes.c_char_p]
                library.iifp_cache_change_token.restype = ctypes.c_char_p
            if hasattr(library, "iifp_normalize_change_time"):
                library.iifp_normalize_change_time.argtypes = [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_int64, ctypes.c_int64]
                library.iifp_normalize_change_time.restype = ctypes.c_char_p
            _library = library
            return library
    raise RuntimeError("iiFileProvider native library >= 0.7 is required; set IIFILEPROVIDER_NATIVE_LIBRARY")


def _path(path):
    value = os.fsencode(path)
    if b"\0" in value:
        raise ValueError("File paths must not contain NUL")
    return value


def _check(result):
    if result < 0:
        library = _native()
        raise OSError(library.iifp_last_errno() or errno.EIO, library.iifp_last_error().decode("utf-8", "replace"))


class _PresentationFileIO(io.FileIO):
    def __init__(self, descriptor, mode, path, library):
        self._presentation_path = _path(Path(path).absolute())
        self._presentation_library = library
        super().__init__(descriptor, mode, closefd=True)

    def close(self):
        completed = not self.closed and self.writable()
        super().close()
        if completed:
            self._presentation_library.iifp_present_later(self._presentation_path)


def presentation(path, *, prepare=False):
    """Verified native details and embedded preview, or None while missing/stale."""
    import json
    library = _native()
    if prepare:
        _check(library.iifp_present(_path(path)))
    document = library.iifp_presentation(_path(path))
    return json.loads(document) if document else None


def wait_for_presentations(timeout=15.0):
    return bool(_native().iifp_presentation_wait(max(0, int(timeout * 1000))))


def normalize_change_time(device, index, seconds, nanoseconds):
    """Normalize our own preview attribute writes over a pinned native stat."""
    library = _native()
    if not hasattr(library, "iifp_normalize_change_time"):
        raise RuntimeError("Native iiFileProvider >= 0.8.1 is required for change-time normalization")
    value = library.iifp_normalize_change_time(device, index, seconds, nanoseconds)
    if value is None:
        _check(-1)
    return value.decode("utf-8")


def cache_change_token(path):
    """Snapshot native change identity atomically with preview metadata writes."""
    library = _native()
    if not hasattr(library, "iifp_cache_change_token"):
        raise RuntimeError("Native iiFileProvider >= 0.8.2 is required for atomic change tokens")
    value = library.iifp_cache_change_token(_path(path))
    if value is None:
        _check(-1)
    return value.decode("utf-8")


def open(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None,
         closefd=True, opener=None):
    """Python stream semantics with all pathname opens performed by native SDK."""
    if isinstance(file, int):
        return builtins.open(file, mode, buffering, encoding, errors, newline, closefd, opener)
    if (not isinstance(mode, str) or not mode or any(c not in "rwaxbt+" for c in mode)
            or sum(mode.count(c) for c in "rwax") != 1 or len(set(mode)) != len(mode)
            or ("b" in mode and "t" in mode)
            or ("b" in mode and (encoding is not None or errors is not None or newline is not None))):
        raise ValueError("Invalid file stream mode or binary encoding options")
    if opener is not None or not closefd:
        raise ValueError("Pathname streams require provider-owned opening")
    if not any(flag in mode for flag in "wax+"):
        return builtins.open(file, mode, buffering, encoding, errors, newline)
    flags = os.O_RDWR if "+" in mode else os.O_WRONLY
    if "w" in mode: flags |= os.O_CREAT | os.O_TRUNC
    if "a" in mode: flags |= os.O_CREAT | os.O_APPEND
    if "x" in mode: flags |= os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_BINARY", 0) | getattr(os, "O_CLOEXEC", 0)
    descriptor = _native().iifp_open(_path(file), flags, 0o666)
    if descriptor < 0:
        code = ctypes.get_errno() or errno.EIO
        raise OSError(code, os.strerror(code), os.fspath(file))
    os.set_inheritable(descriptor, False)
    raw = None
    try:
        raw = _PresentationFileIO(descriptor, mode.replace("t", "").replace("b", "") + "b", file, _native())
        if buffering == 0:
            if "b" not in mode:
                raise ValueError("can't have unbuffered text I/O")
            return raw
        if buffering < -1:
            raise ValueError("invalid buffering size")
        size = io.DEFAULT_BUFFER_SIZE if buffering in (-1, 1) else buffering
        buffered = io.BufferedRandom(raw, size) if "+" in mode else io.BufferedWriter(raw, size)
        if "b" in mode:
            return buffered
        stream = io.TextIOWrapper(buffered, encoding=encoding, errors=errors, newline=newline,
                                  line_buffering=buffering == 1)
        stream.mode = mode
        return stream
    except BaseException:
        if raw is not None:
            raw.close()
        else:
            os.close(descriptor)
        raise


@contextlib.contextmanager
def staged_path(destination, *, overwrite=True):
    """A leased, suffix-preserving temporary path for PIL, FFmpeg or checkpoints.

    Publication occurs only after a successful encoder; failure retains the old
    destination. Exclusive publication also checks races after preflight.
    """
    library = _native()
    stage = library.iifp_stage(_path(destination))
    if not stage:
        _check(-1)
    try:
        yield Path(os.fsdecode(library.iifp_stage_path(stage)))
        _check(library.iifp_stage_publish(stage, int(overwrite)))
    finally:
        library.iifp_stage_discard(stage)


def write_bytes(path, data):
    with staged_path(path) as temporary:
        with open(temporary, "wb") as stream:
            return stream.write(data)


def write_text(path, data, encoding="utf-8", errors=None, newline=None):
    with staged_path(path) as temporary:
        with open(temporary, "w", encoding=encoding, errors=errors, newline=newline) as stream:
            return stream.write(data)


def mkdir(path, mode=0o777, parents=False, exist_ok=False):
    value = Path(path)
    if exist_ok and value.is_dir():
        return
    _check(_native().iifp_mkdir(_path(value), int(parents)))
    if mode != 0o777:
        os.chmod(value, mode)


def copyfile(source, destination, *, overwrite=True):
    _check(_native().iifp_copy(_path(source), _path(destination), int(overwrite)))
    return os.fspath(destination)


def copy2(source, destination, *, follow_symlinks=True):
    if not follow_symlinks:
        raise ValueError("Provider copies require a regular file")
    if Path(destination).is_dir():
        destination = Path(destination) / Path(source).name
    result = copyfile(source, destination)
    shutil.copystat(source, destination)
    return result


def publish(source, destination, *, overwrite=True):
    _check(_native().iifp_publish(_path(source), _path(destination), int(overwrite)))


def replace(source, destination):
    publish(source, destination, overwrite=True)


def rename(source, destination):
    replace(source, destination)
    return Path(destination)


def link(source, destination):
    publish(source, destination, overwrite=False)


def copytree(source, destination, **kwargs):
    kwargs["copy_function"] = copy2
    return shutil.copytree(source, destination, **kwargs)


def move(source, destination, **kwargs):
    return shutil.move(source, destination, copy_function=copy2, **kwargs)


def sqlite_connect(path, *args, **kwargs):
    import sqlite3
    if os.fspath(path) != ":memory:" and not kwargs.get("uri", False) and not Path(path).exists():
        with open(path, "ab"):
            pass
    return sqlite3.connect(path, *args, **kwargs)


def encode_at(index, encoder, *args, **kwargs):
    arguments = list(args)
    with staged_path(arguments[index]) as temporary:
        arguments[index] = str(temporary)
        return encoder(*arguments, **kwargs)


def save_image(image, destination, *args, **kwargs):
    with staged_path(destination) as temporary:
        return image.save(temporary, *args, **kwargs)


def encode(destination, encoder, *args, **kwargs):
    """Invoke a path-only codec with a provider-owned output path."""
    with staged_path(destination) as temporary:
        return encoder(str(temporary), *args, **kwargs)


def mkstemp(suffix="", prefix="tmp", dir=None, text=False):
    parent = Path(dir if dir is not None else _tempfile.gettempdir())
    for _ in range(128):
        name = parent / (prefix + uuid.uuid4().hex + suffix)
        descriptor = _native().iifp_open(_path(name), os.O_RDWR | os.O_CREAT | os.O_EXCL
            | getattr(os, "O_BINARY", 0) | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOINHERIT", 0), 0o600)
        if descriptor >= 0:
            os.set_inheritable(descriptor, False)
            return descriptor, str(name)
        code = ctypes.get_errno()
        if code != errno.EEXIST:
            raise OSError(code, os.strerror(code), str(name))
    raise FileExistsError("Cannot reserve a temporary file")


def mkdtemp(suffix="", prefix="tmp", dir=None):
    parent = Path(dir if dir is not None else _tempfile.gettempdir())
    for _ in range(128):
        name = parent / (prefix + uuid.uuid4().hex + suffix)
        if name.exists():
            continue
        try:
            mkdir(name, mode=0o700)
            return str(name)
        except FileExistsError:
            continue
    raise FileExistsError("Cannot reserve a temporary directory")


class TemporaryDirectory:
    def __init__(self, suffix="", prefix="tmp", dir=None, ignore_cleanup_errors=False):
        self.name = mkdtemp(suffix, prefix, dir)
        self.ignore_cleanup_errors = ignore_cleanup_errors
        self._cleaned = False
    def __enter__(self):
        return self.name
    def cleanup(self):
        _native().iifp_presentation_cancel(_path(self.name))
        if self._cleaned:
            return
        try:
            shutil.rmtree(self.name, ignore_errors=self.ignore_cleanup_errors)
        except FileNotFoundError:
            pass
        self._cleaned = True
    def __exit__(self, *_):
        self.cleanup()


def is_control_path(path):
    name = Path(path).name.lower()
    return (name in {".iisacc-file-provider", ".iisacc-temporary.lock"}
            or name.startswith(".iifile-stage-") or name.endswith((".iisacc-preview", ".iisacc.json")))


def iterdir(path):
    return (entry for entry in Path(path).iterdir() if not is_control_path(entry))


def rglob(path, pattern):
    root = Path(path)
    return (entry for entry in root.rglob(pattern)
            if not any(is_control_path(part) for part in entry.relative_to(root).parts))


@contextlib.contextmanager
def NamedTemporaryFile(mode="w+b", buffering=-1, encoding=None, newline=None,
                       suffix="", prefix="tmp", dir=None, delete=True, errors=None):
    descriptor, name = mkstemp(suffix, prefix, dir)
    try:
        with os.fdopen(descriptor, mode, buffering, encoding, errors, newline) as stream:
            # Codec callers require a pathname rather than the descriptor name.
            class NamedStream:
                def __getattr__(self, key): return getattr(stream, key)
                @property
                def name(self): return name
            yield NamedStream()
    finally:
        if delete:
            Path(name).unlink(missing_ok=True)
