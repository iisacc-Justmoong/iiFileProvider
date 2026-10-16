"""Native-backed worker publication, rollback and stream contract."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import unittest
os.environ["IIFILEPROVIDER_NATIVE_LIBRARY"] = sys.argv.pop(1)
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
import ii_file_provider as provider
TEST_ROOT = sys.argv.pop(1)


class ProviderTest(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "darwin", "macOS application bundle layout")
    def test_bundled_worker_discovers_its_native_library_without_a_global_sdk(self):
        # Imported runtime installation includes SONAME files, not the unversioned
        # linker symlink. Exercise SDK lib/ and application Frameworks/ layouts.
        for layout in ("lib", "Frameworks"):
            with self.subTest(layout=layout), provider.TemporaryDirectory(dir=TEST_ROOT) as root:
                contents = Path(root) / "Worker.app" / "Contents"
                workers = contents / "Resources" / "workers"
                libraries = contents / layout
                workers.mkdir(parents=True)
                libraries.mkdir()
                shutil.copytree(Path(provider.__file__).parent, workers / "ii_file_provider",
                                ignore=shutil.ignore_patterns("__pycache__"))
                library = Path(os.environ["IIFILEPROVIDER_NATIVE_LIBRARY"]).resolve()
                shutil.copy2(library, libraries / library.name)
                environment = {key: value for key, value in os.environ.items()
                               if not key.startswith(("IIFILEPROVIDER_", "DYLD_"))}
                environment["PYTHONPATH"] = str(workers)
                target = Path(root) / "bundled.png"
                script = "import pathlib, sys, ii_file_provider as p; p.write_bytes(sys.argv[1], b'bundled'); assert pathlib.Path(p._native()._name).parent == pathlib.Path(sys.argv[2])"
                subprocess.run([sys.executable, "-S", "-c", script, str(target), str(libraries)],
                               env=environment, check=True, timeout=30)
                self.assertEqual(target.read_bytes(), b"bundled")

    def test_relative_publication_keeps_the_original_destination(self):
        previous = Path.cwd()
        with provider.TemporaryDirectory(dir=TEST_ROOT) as root:
            root = Path(root).resolve()
            elsewhere = root / "elsewhere"
            provider.mkdir(elsewhere)
            try:
                os.chdir(root)
                with provider.staged_path("relative.png", overwrite=False) as stage:
                    provider.write_bytes(stage, b"encoded")
                    os.chdir(elsewhere)
                self.assertEqual((root / "relative.png").read_bytes(), b"encoded")
                self.assertFalse((elsewhere / "relative.png").exists())
            finally:
                os.chdir(previous)

    def test_moved_temporary_directory_cleanup_is_idempotent(self):
        import shutil
        temporary = provider.TemporaryDirectory(dir=TEST_ROOT)
        published = Path(temporary.name + "-published")
        try:
            provider.rename(temporary.name, published)
            temporary.cleanup()
            temporary.cleanup()
            self.assertTrue(published.is_dir())
        finally:
            shutil.rmtree(published, ignore_errors=True)

    def test_publication_and_codec_failure(self):
        with provider.TemporaryDirectory(dir=TEST_ROOT) as root:
            target = Path(root) / "생성.png"
            provider.write_bytes(target, b"original")
            with self.assertRaises(RuntimeError):
                with provider.staged_path(target) as stage:
                    self.assertEqual(stage.suffix, ".png")
                    provider.write_bytes(stage, b"partial")
                    raise RuntimeError("codec failed")
            self.assertEqual(target.read_bytes(), b"original")
            with self.assertRaises(FileExistsError):
                with provider.staged_path(target, overwrite=False) as stage:
                    provider.write_bytes(stage, b"racing replacement")
            self.assertEqual(target.read_bytes(), b"original")
            with provider.open(target, "ab") as stream:
                stream.write(b"-append")
            self.assertEqual(target.read_bytes(), b"original-append")
            with self.assertRaises(FileExistsError):
                provider.open(target, "xb")
            provider.copyfile(target, Path(root) / "copy.png", overwrite=False)
            self.assertEqual((Path(root) / "copy.png").read_bytes(), target.read_bytes())
            self.assertTrue(provider.wait_for_presentations())
            details = provider.presentation(target)
            self.assertIsNotNone(details)
            self.assertEqual(details["details"]["size"], len(b"original-append"))
            self.assertEqual(details["preview"]["kind"], "text")
            self.assertFalse(list(Path(root).glob(".iifile-stage-*")))


if __name__ == "__main__":
    unittest.main()
