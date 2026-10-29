"""Reject SDK dependency cycles and upward dependencies from the storage owner."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
projects = {p.name: p for p in root.iterdir() if (p / "CMakeLists.txt").is_file()}
graph = {}
for name, project in projects.items():
    files = [project / "CMakeLists.txt", *project.glob("cmake/*Config.cmake.in")]
    content = "\n".join(p.read_text() for p in files)
    references = set(re.findall(r"find_(?:package|dependency)\(\s*(\w+)", content))
    if references & {"iiFileProviderNative", "iiFileProviderCatalog", "iiFileProviderTemporary", "iiFileProviderObjects", "iiFileProviderSwift"}:
        references.add("iiFileProvider")
    graph[name] = sorted((references & projects.keys()) - {name})
assert graph["iiFileProvider"] == [], "iiFileProvider must not depend on another iisacc SDK"
assert graph["iiSDKCore"] == [], "Core must not depend on a feature SDK"
adapter = root / "iiFileProvider/example/CoreIO"
if adapter.exists():
    references = set(re.findall(r"find_(?:package|dependency)\(\s*(\w+)",
        (adapter / "iiFileProviderCoreIOConfig.cmake.in").read_text()))
    assert references == {"iiSDKCore", "iiFileProviderNative"}, "CoreIO must sit above both independent libraries"
active, complete, order = [], set(), []

def visit(name):
    assert name not in active, "dependency cycle: " + " -> ".join([*active, name])
    if name in complete:
        return
    active.append(name)
    for child in graph[name]:
        visit(child)
    active.pop()
    complete.add(name)
    order.append(name)

for name in sorted(graph):
    visit(name)
provider = projects["iiFileProvider"]
for base in (provider / "src", provider / "Native/src"):
    for file in [*base.rglob("*.h"), *base.rglob("*.cpp")]:
        source=file.read_text()
        assert not re.search(r'\b(?:QFile|QSaveFile|QTemporaryFile)\b', source), f"Qt file engine returned to its owner: {file}"
for file in [*(provider / "Native/src").glob("*.h"), *(provider / "Native/src").glob("*.cpp")]:
    assert not re.search(r'#include\s*[<"](?:Qt\w*|Q[A-Z]\w*)',file.read_text()), f"Native storage imports Qt: {file}"
for file in [*provider.glob("src/**/*.h"), *provider.glob("src/**/*.cpp")]:
    includes = re.findall(r'#include\s*[<"](ii\w+)', file.read_text())
    assert all(name.startswith("iiFileProvider") for name in includes), f"upward include: {file}"
for file in [*adapter.glob("App/**/*.h"), *adapter.glob("App/**/*.cpp")]:
    includes = re.findall(r'#include\s*[<"](ii\w+)', file.read_text())
    assert all(name.startswith(("iiSDKCore", "iiFileProvider")) for name in includes), f"unexpected adapter dependency: {file}"
for relative in [
    "iiSharedCanvas/src/File/DocumentFile.cpp", "iiSharedCanvas/tools/IiscInput.cpp",
    "iiSharedCanvas/src/Media/MediaIo.cpp", "iiCSMIDI/src/MidiDocument.cpp",
    "iiPaintEngine/src/BitmapFile/BitmapFile.cpp", "iiXml/Src/Input/GetFile.cpp",
    "iiXml/Src/Parser/FileParser.cpp", "iiGeneralDocument/src/Pdf/PdfDocumentReader.cpp",
    "iiGeneralDocument/src/Pdf/PdfDocumentWriter.cpp",
    "iiSocietyHelper/src/MediaExport.cpp",
]:
    file = root / relative
    if file.exists():
        source = file.read_text()
        assert not re.search(r'#include\s*[<"](?:QFile|QSaveFile|fstream|sqlite3.h)[>"]', source), f"storage bypass: {file}"
for relative in ("src/Catalog/FileCatalog.h", "src/Catalog/FileCatalog.cpp", "tools/FileProvider.cpp",
                 "src/Objects/ObjectStore.h", "src/Objects/ObjectStore.cpp", "src/Objects/ObjectMetadataCodec_p.h", "src/Objects/ObjectMetadataCodec.cpp",
                 "src/Objects/OntologyFileSystem.h", "src/Objects/OntologyFileSystem.cpp", "src/Objects/OntologySchema_p.h", "src/Objects/ObjectSqlite_p.h"):
    source = (provider / relative).read_text()
    assert not re.search(r'#include\s*[<"](?:Qt\w*|Q[A-Z]\w*)', source), f"native catalog imports Qt: {relative}"
print(f"{len(graph)} SDK dependency declarations form a DAG; iiFileProvider has no upward SDK edges.")
print("Build order: " + " -> ".join(order))
