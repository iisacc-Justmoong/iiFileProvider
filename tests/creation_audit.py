from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("creation_audit", Path(__file__).resolve().parents[1] / "tools/audit_file_creation.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
WORK = Path(sys.argv.pop(1))


class CreationAuditTest(unittest.TestCase):
    def test_native_qt_python_swift_and_android_factories(self):
        self.assertEqual(audit.category(Path("SDK/iiServerHost/tools/ftp_server.py")), "runtime")
        self.assertEqual(audit.category(Path("SDK/Example/src/runtimes/worker.py")), "runtime")
        self.assertTrue(audit.inspect(Path("writer.cpp"), 'std::ofstream output(path);'))
        self.assertTrue(audit.inspect(Path("writer.cpp"), 'QSaveFile output(path);'))
        self.assertTrue(audit.inspect(Path("writer.cpp"), 'std::filesystem::copy_file(source, target);'))
        self.assertTrue(audit.inspect(Path("writer.cpp"), '::open(path,\n O_CREAT | O_EXCL, 0600);'))
        self.assertTrue(audit.inspect(Path("writer.cpp"), 'sqlite3_open_v2(path, &db, flags, nullptr);'))
        self.assertFalse(audit.inspect(Path("writer.cpp"), 'iiFileProvider::native::OutputFile output(path);'))
        self.assertFalse(audit.inspect(Path("writer.cpp"), 'iiFileProvider::native::openFile(path, O_CREAT | O_EXCL, 0600);'))
        self.assertFalse(audit.inspect(Path("writer.cpp"), 'ConfinedMount::self().open(path, O_CREAT, 0600);'))
        self.assertTrue(audit.inspect(Path("writer.py"), 'path.write_bytes(b"output")'))
        self.assertFalse(audit.inspect(Path("writer.py"), 'provider.write_bytes(path, b"output")'))
        self.assertTrue(audit.inspect(Path("writer.java"), 'new FileOutputStream(path);'))
        self.assertTrue(audit.inspect(Path("writer.swift"), 'FileManager.default.createFile(atPath: path, contents: data)'))
        borrowed = '''with provider.staged_path(target) as temporary:
    image.save(temporary, format="PNG")
    with provider.open(temporary, "wb") as device, wave.open(device, "wb") as stream:
        stream.writeframes(samples)
'''
        self.assertFalse(audit.inspect(Path("writer.py"), borrowed))

    def test_nested_model_sources_and_archived_products_are_inspected(self):
        with tempfile.TemporaryDirectory(dir=WORK) as folder:
            root = Path(folder)
            for relative in ("SDK/Example/src/io.cpp", "Product/App/src/models/writer.cpp", "Product/.Deprecated/Old/src/save.cpp"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("QSaveFile output(path);\n")
            fixture = root / "Product/App/tests/writer.cpp"
            fixture.parent.mkdir(parents=True)
            fixture.write_text("QSaveFile fixture(path);\n")
            report = audit.audit(root)
            self.assertEqual(len(report["violations"]), 3)
            self.assertEqual(report["unreadable"], [])
            self.assertEqual(report["counts"]["fixture"], 1)


if __name__ == "__main__":
    unittest.main()
