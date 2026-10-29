from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
project = (root / "CMakeLists.txt").read_text()
assert not re.search(r"add_subdirectory\s*\(\s*(?:CoreIO|example/CoreIO)", project, re.I), \
    "CoreIO must be built by a separate consumer project"
assert not (root / "CoreIO").exists(), "legacy adapter still sits in the module build"
adapter = root / "example/CoreIO"
assert (adapter / "CMakeLists.txt").is_file(), "consumer adapter is missing"
consumer = (adapter / "CMakeLists.txt").read_text()
assert "find_package(iiSDKCore" in consumer and "find_package(iiFileProviderNative" in consumer
for base in (root / "src", root / "Native/src"):
    for path in base.rglob("*"):
        if path.suffix in {".h", ".cpp", ".mm"}:
            assert not re.search(r"#\s*(?:include|import)\s*[<\"]iiSDKCore/", path.read_text()), path
print("Core I/O is a separate application-level composition")
