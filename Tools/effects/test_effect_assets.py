import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("efkbuild", Path(__file__).with_name("efkbuild.py"))
efkbuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(efkbuild)


class EffectInstallTest(unittest.TestCase):
    def test_install_copies_effects_and_assets_without_sidecar(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2] / "build") as directory:
            root = Path(directory)
            out = root / "out"
            destination = root / "installed"
            out.mkdir(parents=True)
            (out / "spark.efkefc").write_bytes(b"effect")
            texture = out / "spark.png"
            texture.write_bytes(b"png")
            assets = {Path("Texture/spark.png"): texture}

            self.assertTrue(efkbuild.install(["spark"], assets, out, destination))
            self.assertEqual((destination / "spark.efkefc").read_bytes(), b"effect")
            self.assertEqual((destination / "Texture/spark.png").read_bytes(), b"png")
            self.assertFalse((destination / "spark.effect").exists())

    def test_install_rejects_destination_outside_repo(self):
        with tempfile.TemporaryDirectory() as outside_dir:
            with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2] / "build") as directory:
                out = Path(directory) / "out"
                out.mkdir(parents=True)
                (out / "spark.efkefc").write_bytes(b"effect")
                outside = Path(outside_dir) / "installed"
                self.assertFalse(efkbuild.install(["spark"], {}, out, outside))
                self.assertFalse((outside / "spark.efkefc").exists())


if __name__ == "__main__":
    unittest.main()
