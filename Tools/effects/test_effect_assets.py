import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("efkbuild", Path(__file__).with_name("efkbuild.py"))
efkbuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(efkbuild)


class EffectAssetGeneration(unittest.TestCase):
    def test_new_assets_get_distinct_identities(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2] / "build") as directory:
            root = Path(directory)
            identifiers = []
            for name in ("first", "second"):
                effect = root / (name + ".efkefc")
                effect.write_bytes(b"effect")
                self.assertTrue(efkbuild.ensure_effect_asset(effect, root))
                identifiers.append(json.loads(effect.with_suffix(".effect").read_text(encoding="utf-8"))["id"])
            self.assertNotEqual(identifiers[0], identifiers[1])

    def test_rebuild_keeps_identity_label_and_random_basis(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2] / "build") as directory:
            root = Path(directory)
            effect = root / "Assets/Effects/spark.efkefc"
            effect.parent.mkdir(parents=True)
            effect.write_bytes(b"effect")
            self.assertTrue(efkbuild.ensure_effect_asset(effect, root))
            metadata = effect.with_suffix(".effect")
            original = json.loads(metadata.read_text(encoding="utf-8"))
            self.assertEqual(len(original["id"]), 32)
            self.assertEqual(original["source"], "Assets/Effects/spark.efkefc")
            original["label"] = "名前を変えた素材"
            metadata.write_text(json.dumps(original, ensure_ascii=False), encoding="utf-8")
            self.assertTrue(efkbuild.ensure_effect_asset(effect, root))
            self.assertEqual(json.loads(metadata.read_text(encoding="utf-8")), original)

    def test_malformed_metadata_is_reported_without_replacing_it(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[2] / "build") as directory:
            root = Path(directory)
            effect = root / "sample.efkefc"
            effect.write_bytes(b"effect")
            metadata = effect.with_suffix(".effect")
            metadata.write_text("broken", encoding="utf-8")
            self.assertFalse(efkbuild.ensure_effect_asset(effect, root))
            self.assertEqual(metadata.read_text(encoding="utf-8"), "broken")


if __name__ == "__main__":
    unittest.main()
