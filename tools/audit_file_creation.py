#!/usr/bin/env python3
"""Audit repository-owned file creation, including native and Python AI outputs.

Generated builds, dependencies and test fixtures are counted separately. Deprecated
products are inspected. The audit never executes source files or edits callers.
External encoders must receive a provider-owned stream, file or private directory;
their internal implementation and compiler/installer artifacts are not application
file factories. This is a source boundary gate, not a syscall sandbox.
"""
from __future__ import annotations
import argparse
import ast
import base64
from collections import Counter
import json
import os
from pathlib import Path
import re
import stat
import shutil
import subprocess

EXCLUDED = {".git", ".build", ".venv", "venv", ".cache", ".generation-resources",
            "node_modules", "vendor", "third_party", "ThirdParty", "third-party",
            "external", "_deps", "Pods", "site-packages", "__pycache__", "target"}
EXTENSIONS = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".c", ".m", ".mm",
              ".swift", ".py", ".java", ".kt", ".rs", ".js", ".ts", ".svelte"}
CPP = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".c", ".m", ".mm"}


def category(path):
    parts = path.parts
    if "iiFileProvider" in parts:
        return "provider"
    if any(x.lower() in {"tests", "test", "fixtures"} for x in parts) or path.name.startswith(("tst_", "test_")) or "Tests." in path.name or ".test." in path.name:
        return "fixture"
    if path.as_posix() in {"SDK/iiServerHost/tools/ftp_server.py", "SDK/iiServerHost/tools/provider_fs.py",
            "SDK/iiLocalDiffusion/reference/generate.py", "SDK/iiLocalDiffusion/reference/merge.py",
            "SDK/iiLocalDiffusion/reference/convert.py"}:
        return "runtime"
    # Build/install/scaffolding programs bootstrap SDKs before a provider exists.
    # The public C++ iiSDKBuilder itself is a runtime SDK and is inspected.
    if path.suffix == ".py" and not ("iiLocalDiffusion" in parts and "diffusers" in parts) and not any(x in parts for x in ("runtime", "runtimes", "workers", "worker")):
        return "tooling"
    if any(x in parts for x in ("cmake", "tools", "scripts", "rust-cli")) and path.suffix not in CPP:
        return "tooling"
    return "runtime"


def sources(root):
    for surface in ("SDK", "Product"):
        for directory, children, files in os.walk(root / surface, followlinks=False):
            children[:] = sorted(x for x in children if x not in EXCLUDED
                and not x.startswith(("build", "cmake-build"))
                and (not x.startswith(".") or x == ".Deprecated"))
            for name in sorted(files):
                path = Path(directory) / name
                if path.suffix in EXTENSIONS:
                    yield path


def python_writes(source):
    tree = ast.parse(source)
    violations = []
    def walk(node, borrowed=frozenset()):
        if isinstance(node, (ast.With, ast.AsyncWith)):
            names = set(borrowed)
            for item in node.items:
                call = item.context_expr
                if isinstance(call, ast.Call) and ast.unparse(call.func) in {"provider.staged_path", "provider.open"} and isinstance(item.optional_vars, ast.Name):
                    names.add(item.optional_vars.id)
                if (isinstance(call, ast.Call) and ast.unparse(call.func) == "zipfile.ZipFile"
                        and call.args and isinstance(call.args[0], ast.Name)
                        and call.args[0].id in names and isinstance(item.optional_vars, ast.Name)):
                    names.add(item.optional_vars.id)
            for child in ast.iter_child_nodes(node):
                walk(child, frozenset(names))
            return
        if isinstance(node, ast.Call):
            function = ast.unparse(node.func)
            owned = function.startswith(("provider.", "ii_file_provider.")) or (
                isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name)
                and node.func.value.id in borrowed)
            owned |= (function in {"wave.open", "zipfile.ZipFile"} and bool(node.args)
                and isinstance(node.args[0], ast.Name) and node.args[0].id in borrowed)
            bad = False
            if isinstance(node.func, ast.Attribute):
                name = node.func.attr
                bad = name in {"write_text", "write_bytes", "mkdir", "touch"} and not owned
                bad |= function in {"shutil.copyfile", "shutil.copy2", "shutil.copytree", "shutil.move", "os.replace", "os.rename", "os.link", "os.makedirs", "os.mkdir", "sqlite3.connect"}
                bad |= function in {"tempfile.mkstemp", "tempfile.mkdtemp", "tempfile.NamedTemporaryFile", "tempfile.TemporaryDirectory"}
                if name == "open" and not owned:
                    modes = [x.value for x in node.args if isinstance(x, ast.Constant) and isinstance(x.value, str)]
                    modes += [x.value.value for x in node.keywords if x.arg == "mode" and isinstance(x.value, ast.Constant) and isinstance(x.value.value, str)]
                    bad |= any(re.fullmatch(r"[rwax][bt+]*", mode) and any(c in mode for c in "wax+") for mode in modes)
                if name == "save" and not owned and node.args:
                    target = node.args[0]
                    bad |= not (isinstance(target, ast.Name) and target.id in borrowed | {"buffer", "stream", "output", "out"})
            elif function == "open":
                mode = node.args[1] if len(node.args) > 1 else next((x.value for x in node.keywords if x.arg == "mode"), None)
                bad = isinstance(mode, ast.Constant) and isinstance(mode.value, str) and any(c in mode.value for c in "wax+")
            if bad:
                violations.append((node.lineno, function))
        for child in ast.iter_child_nodes(node):
            walk(child, borrowed)
    walk(tree)
    return violations


def inspect(path, source):
    found = []
    if path.suffix == ".py":
        return python_writes(source)
    patterns = []
    if path.suffix in CPP:
        patterns = [r"\b(?:QSaveFile|QTemporaryFile|QTemporaryDir|QLockFile)\b",
                    r"\bstd::(?:ofstream|fstream)\b", r"\bQFile::(?:copy|resize)\s*\(",
                    r"\b(?:std::filesystem|filesystem|fs)::(?:create_directories|create_directory|copy_file|create_hard_link|rename)\s*\(",
                    r"(?<![\w:])(?:sqlite3_open|sqlite3_open_v2)\s*\(",
                    r"\bQDir\b.*\.(?:mkpath|mkdir)\(",
                    r"(?<![\w:])(?:::)?(?:fopen|mkdirat)\s*\(",
                    r"(?<![\w:])(?:CreateDirectoryW|NtCreateFile)\s*\("]
        for match in re.finditer(r"(?<![\w:.>])(?:::)?(?:open|openat)\s*\([^;{}]{0,700}\bO_CREAT\b", source):
            found.append((source.count("\n", 0, match.start()) + 1, match.group(0).strip()[:300]))
    elif path.suffix == ".swift":
        patterns = [r"FileManager\.default\.(?:createFile|createDirectory|copyItem)\(",
                    r"(?<!Provider)\bmanager\.(?:createFile|createDirectory|copyItem)\(",
                    r"(?<![\w.])open\([^\n]*O_CREAT", r"(?<![\w.])openat\([^\n]*(?:O_CREAT|createFlags)"]
    elif path.suffix in {".java", ".kt"}:
        patterns = [r"new\s+(?:FileOutputStream|AtomicFile)\s*\(", r"ParcelFileDescriptor\.open\(", r"\b\w+\.(?:createNewFile|mkdir|mkdirs)\(\)"]
    elif path.suffix == ".rs":
        # Inline #[cfg(test)] modules contain fixture factories.
        source = source.split("#[cfg(test)]", 1)[0]
        patterns = [r"\b(?:std::)?fs::write\(", r"\bFile::create\(", r"\bOpenOptions::new\("]
    elif path.suffix in {".js", ".ts", ".svelte"}:
        patterns = [r"\b(?:writeFileSync|writeFile|createWriteStream|createWritable|showSaveFilePicker)\("]
    for number, line in enumerate(source.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith(("//", "*", "/*", "# ")):
            continue
        if any(re.search(pattern, line) for pattern in patterns):
            found.append((number, stripped[:300]))
    return found


def rg_sources(paths):
    """Read bounded batches exactly once; retain bytes/newlines and fail closed."""
    for offset in range(0, len(paths), 64):
        batch = paths[offset:offset+64]
        values, errors = {}, {}
        try:
            result = subprocess.run(["rg", "--text", "--multiline", "--json", "--no-ignore",
                                     r"(?s)\A.*\z", "--", *(str(path) for path in batch)],
                                    capture_output=True, timeout=30)
            if result.returncode not in (0, 1, 2):
                raise OSError(result.stderr.decode("utf-8", "replace"))
            for line in result.stdout.splitlines():
                item = json.loads(line)
                if item["type"] != "match": continue
                data = item["data"]
                name = data["path"]
                name = name.get("text") or base64.b64decode(name["bytes"]).decode("utf-8")
                body = data["lines"]
                try:
                    values[name] = body["text"] if "text" in body else base64.b64decode(body["bytes"]).decode("utf-8")
                except UnicodeError as error:
                    errors[name] = str(error)
            for path in batch:
                name = str(path)
                if name in errors: yield path, None, errors[name]
                elif name in values: yield path, values[name], None
                else:
                    try:
                        if path.stat().st_size == 0: yield path, "", None
                        else: yield path, None, result.stderr.decode("utf-8", "replace") or "source was not returned by native scanner"
                    except OSError as error: yield path, None, str(error)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            for path in batch: yield path, None, str(error)


def audit(root):
    root = root.resolve()
    counts = Counter()
    violations, unreadable = [], []
    # Native rg scans candidate tokens without opening every renderer/model
    # implementation in Python. The fallback retains standalone portability.
    candidates = None
    if shutil.which("rg"):
        query = ["rg", "--files-with-matches", "--hidden", "--no-ignore",
                 r"QSaveFile|QTemporaryFile|QTemporaryDir|QLockFile|QFile|\bstd::(?:ofstream|fstream)\b|create_director|copy_file|create_hard_link|\bfs::rename|std::filesystem::rename|fopen|mkdir|O_CREAT|CreateDirectory|NtCreateFile|sqlite3_open|sqlite3.connect|tempfile\.|write_text|write_bytes|\.save\(|\bopen\([^\n]*[\x27\x22][wax]|createFile|createDirectory|copyItem|FileOutputStream|AtomicFile|writeFile|createWritable|OpenOptions|File::create",
                 "SDK", "Product"]
        for extension in EXTENSIONS:
            query += ["-g", "*" + extension]
        for name in EXCLUDED:
            query += ["-g", "!**/" + name + "/**"]
        query += ["-g", "!**/build*/**", "-g", "!**/cmake-build*/**"]
        result = subprocess.run(query, cwd=root, capture_output=True, text=True)
        if result.returncode not in (0, 1):
            return {"counts": {}, "violations": [], "unreadable": [result.stderr]}
        candidates = {str(root / path) for path in result.stdout.splitlines()}
    selected = []
    for path in sources(root):
        relative = path.relative_to(root)
        kind = category(relative)
        counts[kind] += 1
        if kind != "runtime":
            continue
        if candidates is not None and str(path) not in candidates:
            continue
        try:
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_size > 4_000_000:
                unreadable.append(str(relative)); continue
            if candidates is not None: selected.append(path)
            else:
                value = path.read_text(encoding="utf-8")
                for line, text in inspect(path, value):
                    violations.append({"path": str(relative), "line": line, "writer": text})
        except (OSError, UnicodeError, SyntaxError, subprocess.TimeoutExpired) as error:
            unreadable.append(f"{relative}: {error}")
    for path, value, error in rg_sources(selected):
        relative = path.relative_to(root)
        if error:
            unreadable.append(f"{relative}: {error}"); continue
        try:
            for line, text in inspect(path, value):
                violations.append({"path": str(relative), "line": line, "writer": text})
        except (UnicodeError, SyntaxError) as error:
            unreadable.append(f"{relative}: {error}")
    return {"counts": dict(counts), "violations": violations, "unreadable": unreadable}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = audit(args.workspace)
    if args.output:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"counts": result["counts"], "violations": len(result["violations"]), "unreadable": len(result["unreadable"])}))
    if args.check and (result["violations"] or result["unreadable"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
