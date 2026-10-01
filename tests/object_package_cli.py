"""Real-process crash/resume and complete source/payload audit contract."""
import json
import hashlib
import pathlib
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def run(*args):
    result = subprocess.run([str(executable), *args], capture_output=True, text=True, timeout=180)
    events = [json.loads(line) for line in result.stdout.splitlines()]
    return result.returncode, events


executable = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(tempfile.mkdtemp(prefix="object-cli-", dir=pathlib.Path.cwd()))
child = None
try:
    tree = root / "source"
    tree.mkdir()
    first_name = "10-small-000"
    batch_count = 256
    for index in range(batch_count):
        (tree / f"10-small-{index:03}").write_bytes(b"committed batch object")
    # Long enough to kill the process after the first object; bounded fixture I/O.
    # A lexically earlier large file must not delay the small-file checkpoint.
    with (tree / "00-large").open("wb") as source:
        source.truncate(128 * 1024 * 1024)
    package = root / "package"
    checkpoint_pages = "262144"
    args = ["--package", str(package), "--container", "cli-fixture", "--map", f"Files={tree}",
            "--wal-autocheckpoint-pages", checkpoint_pages]
    # CLI validation must fail before inventory, storage or work-session effects.
    for invalid in ("-1", "+1", "1tail", " 1", "2147483648"):
        status, rejected = run(*args, "--wal-autocheckpoint-pages", invalid)
        assert status == 1 and len(rejected) == 1 and rejected[0]["event"] == "error", rejected
        assert not package.exists(), "invalid options must not create a package"
    status, rejected = run(*args, "--inventory", "--audit")
    assert status == 1 and len(rejected) == 1 and rejected[0]["event"] == "error", rejected
    assert not package.exists(), "conflicting modes must not create a package"
    child = subprocess.Popen([str(executable), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    committed = None
    session_key = None
    for line in child.stdout:
        event = json.loads(line)
        if event["event"] == "session":
            session_key = event["key"]
        if event["event"] == "imported":
            committed = event
            child.kill()
            break
    child.communicate(timeout=10)
    assert committed and committed["path"] == f"Files/{first_name}", committed
    assert committed["indexKey"] > 0 and len(committed["sha256"]) == 64, committed
    assert committed["sha256"] == hashlib.sha256(b"committed batch object").hexdigest(), committed
    assert committed["validationKey"].startswith("society-object-v2:"), committed
    assert committed["sessionKey"] == session_key, committed
    assert committed["recordingActor"]["subject"] == "iiFileProvider.packager", committed
    assert committed["authorshipKnown"] is False, committed
    assert child.returncode != 0, "fixture must terminate an active child, not an already completed run"
    status, closed = run("--package", str(package), "--container", "cli-fixture", "--end-session", session_key)
    assert status == 0 and closed == [{"event": "session-ended", "key": session_key}], closed
    status, resumed = run(*args)
    assert status == 0, resumed
    settings = next(item for item in resumed if item["event"] == "settings")
    assert settings["walAutoCheckpointPages"] == int(checkpoint_pages), settings
    summary = resumed[-1]
    assert summary["imported"] == 1 and summary["skipped"] == batch_count, resumed
    preserved = next(item for item in resumed if item["event"] == "skipped")
    assert preserved["key"] == committed["key"] and preserved["version"] == 1
    for field in ("indexKey", "sha256", "validationKey", "sessionKey", "recordingActor", "authorshipKnown"):
        assert preserved[field] == committed[field], (field, preserved, committed)
    status, repeated = run(*args)
    assert status == 0 and repeated[-1]["skipped"] == batch_count + 1 and repeated[-1]["revised"] == 0, repeated
    status, audit = run(*args, "--audit")
    assert status == 0 and audit[-1]["files"] == batch_count + 1 and audit[-1]["verified"] == batch_count + 1
    assert audit[-1]["errors"] == 0 and audit[-1]["cancelled"] is False, audit
    status, index = run("--package", str(package), "--container", "cli-fixture", "--index")
    assert status == 0 and len(index) == 3 and all(item["objects"] == batch_count + 1 for item in index), index
    (tree / first_name).write_bytes(b"changed source")
    status, stale = run(*args, "--audit")
    assert status == 1 and stale[-1]["errors"] == 1 and stale[-1]["verified"] == batch_count, stale
    status, updated = run(*args)
    assert status == 0 and updated[-1]["revised"] == 1 and updated[-1]["skipped"] == batch_count, updated
    changed = next(item for item in updated if item["event"] == "revised")
    assert changed["key"] == committed["key"] and changed["version"] == 2
    assert changed["sha256"] == hashlib.sha256(b"changed source").hexdigest(), changed
    assert changed["indexKey"] == committed["indexKey"] and changed["validationKey"] != committed["validationKey"], changed
    assert (tree / first_name).read_bytes() == b"changed source"
    with sqlite3.connect(package / "objects.sqlite3") as db:
        assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        assert db.execute("SELECT count(*) FROM revisions").fetchone() == (batch_count + 2,)
    print(f"Process kill/resume preserves all {batch_count} committed batch objects; source, revision identity and audit passed")
finally:
    if child is not None and child.poll() is None:
        child.kill()
        child.communicate(timeout=10)
    shutil.rmtree(root)
