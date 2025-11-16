import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.ci import fetch_policy_artifacts as fetch


class FetchPolicyArtifactsTest(unittest.TestCase):
    def test_cli_path_used_when_gh_available(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            download_dir = Path(tmpdir) / "dl"
            extract_dir = Path(tmpdir) / "extract"
            archive_path = download_dir / "artifact.zip"

            with mock.patch("sys.argv", [
                "fetch_policy_artifacts.py",
                "--download-dir",
                str(download_dir),
                "--extract-dir",
                str(extract_dir),
            ]), mock.patch.object(fetch, "detect_sha", return_value="abc123"), mock.patch.object(
                fetch, "detect_branch", return_value="feature"
            ), mock.patch(
                "tools.ci.fetch_policy_artifacts.shutil.which", return_value="/usr/bin/gh"
            ), mock.patch.object(
                fetch, "pick_run_id_cli", return_value=(42, "abc123")
            ) as mock_pick_cli, mock.patch.object(
                fetch, "download_artifact", return_value=archive_path
            ) as mock_download_cli, mock.patch.object(
                fetch, "download_artifact_api"
            ) as mock_download_api, mock.patch.object(
                fetch, "unpack", return_value=extract_dir
            ) as mock_unpack:
                fetch.main()

            mock_pick_cli.assert_called_once_with("gh", "CI", "feature", "abc123", 20, False, None)
            mock_download_cli.assert_called_once_with("gh", 42, "policy-analysis-abc123", download_dir)
            mock_download_api.assert_not_called()
            mock_unpack.assert_called_once_with(archive_path, extract_dir)

    def test_rest_fallback_when_gh_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            download_dir = Path(tmpdir) / "dl"
            extract_dir = Path(tmpdir) / "extract"
            archive_path = download_dir / "artifact.zip"

            argv = [
                "fetch_policy_artifacts.py",
                "--download-dir",
                str(download_dir),
                "--extract-dir",
                str(extract_dir),
            ]
            with mock.patch("sys.argv", argv), mock.patch.object(fetch, "detect_sha", return_value="deadbeef"), mock.patch.object(
                fetch, "detect_branch", return_value="main"
            ), mock.patch(
                "tools.ci.fetch_policy_artifacts.shutil.which", return_value=None
            ), mock.patch.object(
                fetch, "detect_repo_slug", return_value=("nicloadoff", "NicLoadOff")
            ) as mock_slug, mock.patch.object(
                fetch, "pick_run_id_api", return_value=(314, "feedface")
            ) as mock_pick_api, mock.patch.object(
                fetch, "download_artifact_api", return_value=archive_path
            ) as mock_download_api, mock.patch.object(
                fetch, "download_artifact"
            ) as mock_download_cli, mock.patch.object(
                fetch, "unpack", return_value=extract_dir
            ) as mock_unpack, mock.patch.dict(os.environ, {"GITHUB_TOKEN": "ttt"}, clear=True):
                fetch.main()

            mock_slug.assert_called_once_with(None)
            mock_pick_api.assert_called_once_with(
                "nicloadoff", "NicLoadOff", "CI", "main", "deadbeef", 20, "ttt", False, None
            )
            mock_download_cli.assert_not_called()
            mock_download_api.assert_called_once_with(
                "nicloadoff", "NicLoadOff", "ttt", 314, "policy-analysis-feedface", download_dir
            )
            mock_unpack.assert_called_once_with(archive_path, extract_dir)

    def test_latest_flag_updates_artifact_name(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            download_dir = Path(tmpdir) / "dl"
            extract_dir = Path(tmpdir) / "extract"
            archive_path = download_dir / "artifact.zip"
            argv = [
                "fetch_policy_artifacts.py",
                "--latest",
                "--status",
                "success",
                "--download-dir",
                str(download_dir),
                "--extract-dir",
                str(extract_dir),
            ]
            with mock.patch("sys.argv", argv), mock.patch.object(fetch, "detect_sha", return_value="deadbeef"), mock.patch.object(
                fetch, "detect_branch", return_value="develop"
            ), mock.patch(
                "tools.ci.fetch_policy_artifacts.shutil.which", return_value="/usr/bin/gh"
            ), mock.patch.object(
                fetch, "pick_run_id_cli", return_value=(77, "cafebabe")
            ) as mock_pick_cli, mock.patch.object(
                fetch, "download_artifact", return_value=archive_path
            ) as mock_download_cli, mock.patch.object(
                fetch, "unpack", return_value=extract_dir
            ):
                fetch.main()

            mock_pick_cli.assert_called_once_with("gh", "CI", "develop", "deadbeef", 20, True, "success")
            mock_download_cli.assert_called_once_with("gh", 77, "policy-analysis-cafebabe", download_dir)

    def test_list_runs_via_cli(self) -> None:
        sample_runs = [
            {
                "databaseId": 10,
                "headSha": "abcdef00",
                "headBranch": "main",
                "status": "completed",
                "conclusion": "success",
                "name": "CI",
            }
        ]
        with mock.patch(
            "sys.argv",
            ["fetch_policy_artifacts.py", "--list"],
        ), mock.patch.object(fetch, "detect_sha", return_value="ignored"), mock.patch.object(
            fetch, "detect_branch", return_value="main"
        ), mock.patch(
            "tools.ci.fetch_policy_artifacts.shutil.which", return_value="/usr/bin/gh"
        ), mock.patch.object(
            fetch, "load_runs_cli", return_value=sample_runs
        ) as mock_load, mock.patch.object(
            fetch, "print_run_table"
        ) as mock_print, mock.patch.object(
            fetch, "download_artifact"
        ) as mock_download:
            fetch.main()

        mock_load.assert_called_once_with("gh", "CI", "main", 20)
        mock_print.assert_called_once_with(sample_runs, None)
        mock_download.assert_not_called()


if __name__ == "__main__":
    unittest.main()
