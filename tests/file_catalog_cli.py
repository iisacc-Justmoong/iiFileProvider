"""Exercise native provisioning and metadata through the public process boundary."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())


def invoke(root, *args, body=b"", ok=True, raw=False):
    result = subprocess.run([binary, str(root), *map(str, args)], input=body, capture_output=True, timeout=45)
    assert (result.returncode == 0) == ok, (args, result.stderr.decode("utf-8", "replace"))
    if not ok:
        assert "error" in json.loads(result.stderr)
        return
    return result.stdout if raw else json.loads(result.stdout)


with tempfile.TemporaryDirectory(prefix="catalog-cli-", dir=sys.argv[2]) as folder:
    root = Path(folder) / "generated"
    assert invoke(root, "init")["files"] == 0
    payload = b"\x89PNG\x00\xff\r\n" + b"native-data" * 200000
    created = invoke(root, "create", "original.bin", body=payload)
    assert created["metadataWritten"] and (root / "original.bin").read_bytes() == payload
    group = invoke(root, "collection", "Campaign / 캠페인")
    metadata = {"title": "서울 Portrait", "keywords": ["Portrait", "portrait", "Campaign"],
                "rating": 5, "color": "purple", "fields": {"project": "Example"}, "collections": [group["key"]]}
    edited = invoke(root, "annotate", created["fileKey"], created["revision"], body=json.dumps(metadata).encode())
    assert edited["metadata"]["keywords"] == ["Portrait", "Campaign"]
    assert (root / "original.bin").read_bytes() == payload
    invoke(root, "annotate", created["fileKey"], created["revision"], body=b"{}", ok=False)
    for invalid in ({"rating": 2**32}, {"rating": 3.5}, {"rating": -1}, {"rating": True}, {"ratings": 5}):
        invoke(root, "annotate", created["fileKey"], edited["revision"], body=json.dumps(invalid).encode(), ok=False)
    assert invoke(root, "metadata", created["fileKey"])["metadata"] == edited["metadata"]
    conditions = {"text": "서울 por", "allKeywords": ["CAMPAIGN"], "minimumRating": 4, "colors": ["purple"], "collections": [group["key"]]}
    queried = invoke(root, "query", body=json.dumps({"filter": conditions, "limit": 1}).encode())
    assert [entry["key"] for entry in queried["files"]] == [created["fileKey"]]
    assert "metadata" not in queried["files"][0]
    invoke(root, "query", body=b'{"filter":{"minimumSize":1.25}}', ok=False)
    saved = invoke(root, "save-filter", "Approved", body=json.dumps(conditions).encode())
    assert len(invoke(root, "search-filter", saved["key"])["files"]) == 1
    assert invoke(root, "read", created["fileKey"], 1, 7, raw=True) == payload[1:8]
    invoke(root, "read", created["fileKey"], 0, 2**30, ok=False)
    invoke(root, "create", "original.bin", body=b"replacement", ok=False)
    assert (root / "original.bin").read_bytes() == payload
    invoke(root, "create", "../escape", body=b"bad", ok=False)
    assert not (root.parent / "escape").exists()
    other = Path(folder) / "imported"
    imported = invoke(other, "import", root / "original.bin", "copy.bin")
    assert imported["fileKey"] != created["fileKey"] and imported["metadata"] == edited["metadata"]
    assert (other / "copy.bin").read_bytes() == payload
    assert invoke(other, "collections")[0]["name"] == group["name"]
    assert invoke(other, "recover")["pendingMetadata"] == 0
    invoke(Path(folder) / "missing", "query", ok=False)
    assert not (Path(folder) / "missing").exists()
    print("Native CLI streaming, metadata edits, query, saved filters, import and failure preservation verified")
