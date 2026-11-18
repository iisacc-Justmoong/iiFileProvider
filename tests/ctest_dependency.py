"""Prevent Qt Test from returning to the provider's CTest contracts."""

from pathlib import Path
import re
import sys


root = Path(__file__).resolve().parents[1]
paths = [root / "CMakeLists.txt", root / "tests/consumer/CMakeLists.txt"] + [
    root / "tests" / f"{name}.cpp"
    for name in ("authorship", "file_links", "author_contract")
]
forbidden = re.compile(
    r"Qt[56]::Test|find_package\(Qt[56][^\n]*\bTest\b|"
    r"#\s*include\s*[<\"](?:QtTest|QTest|QSignalSpy)|"
    r"\b(?:QTEST_[A-Z_]+|QTest::|QVERIFY(?:2|_THROWS_EXCEPTION)?|"
    r"QCOMPARE|QFETCH|QFINDTESTDATA)\b"
)
violations = []
for path in paths:
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if forbidden.search(line):
            violations.append(f"{path.relative_to(root)}:{number}: {line.strip()}")
if violations:
    print("Qt Test remains in provider contracts:\n" + "\n".join(violations), file=sys.stderr)
    sys.exit(1)
