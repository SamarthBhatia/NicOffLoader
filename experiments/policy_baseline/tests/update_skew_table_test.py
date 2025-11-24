import tempfile
import unittest
from pathlib import Path

import sys

REPO_ROOT = Path(__file__).resolve().parents[3]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from experiments.policy_baseline import update_skew_table as updater  # noqa: E402


class UpdateSkewTableTest(unittest.TestCase):
    def setUp(self) -> None:
        self.expected_csv = (
            Path(__file__).resolve().parent / "data" / "skew_tier_baselines_expected.csv"
        )

    def test_format_and_replace_table(self) -> None:
        rows = updater.load_rows(self.expected_csv)
        table = updater.format_table(rows)
        self.assertIn("| `skew_dag` | baseline | light | 1.0 | 257.5", table)
        with tempfile.TemporaryDirectory() as tmpdir:
            readme = Path(tmpdir) / "README.md"
            readme.write_text(
                "Intro\n"
                + updater.TABLE_HEADER
                + "\n"
                + updater.TABLE_SEPARATOR
                + "\n| `foo` | baseline | light | 1.0 | 0.0 | 0.0 | +0.0 | 0.00 | 0.00 | +0.00 |\nTail\n"
            )
            updater.replace_table(readme, table)
            content = readme.read_text()
            self.assertIn("| `skew_dag_zipf18` | stress | heavy | 1.5 | 329.8", content)
            # Ensure tail content preserved.
            self.assertTrue(content.strip().endswith("Tail"))

    def test_main_dry_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            readme = Path(tmpdir) / "README.md"
            readme.write_text(
                "Intro\n"
                + updater.TABLE_HEADER
                + "\n"
                + updater.TABLE_SEPARATOR
                + "\n| `foo` | baseline | light | 1.0 | 0.0 | 0.0 | +0.0 | 0.00 | 0.00 | +0.00 |\nTail\n"
            )
            # Dry-run should not modify the README.
            updater.main(
                [
                    "--source",
                    str(self.expected_csv),
                    "--readme",
                    str(readme),
                    "--dry-run",
                ]
            )
            self.assertIn("| `foo` | baseline | light | 1.0 | 0.0", readme.read_text())


if __name__ == "__main__":
    unittest.main()
