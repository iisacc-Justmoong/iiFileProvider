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
    graph[name] = sorted((references & projects.keys()) - {name})
assert graph["iiFileProvider"] == [], "iiFileProvider must not depend on another iisacc SDK"
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
for file in [*provider.glob("src/**/*.h"), *provider.glob("src/**/*.cpp")]:
    includes = re.findall(r'#include\s*[<"](ii\w+)', file.read_text())
    assert all(name == "iiFileProvider" for name in includes), f"upward include: {file}"
for relative in [
    "iiSharedCanvas/src/File/DocumentFile.cpp", "iiSharedCanvas/tools/IiscInput.cpp",
    "iiSharedCanvas/src/Media/MediaIo.cpp", "iiCSMIDI/src/MidiDocument.cpp",
    "iiPaintEngine/src/BitmapFile/BitmapFile.cpp", "iiXml/Src/Input/GetFile.cpp",
    "iiXml/Src/Parser/FileParser.cpp", "iiGeneralDocument/src/Pdf/PdfDocumentReader.cpp",
    "iiGeneralDocument/src/Pdf/PdfDocumentWriter.cpp",
]:
    file = root / relative
    if file.exists():
        source = file.read_text()
        assert not re.search(r'#include\s*[<"](?:QFile|QSaveFile|fstream|sqlite3.h)[>"]', source), f"storage bypass: {file}"
print(f"{len(graph)} SDK dependency declarations form a DAG; iiFileProvider has no upward SDK edges.")
print("Build order: " + " -> ".join(order))
