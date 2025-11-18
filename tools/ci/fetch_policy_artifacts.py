#!/usr/bin/env python3
"""Download and unpack the latest policy-analysis artifact from GitHub Actions."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from urllib import parse, request


def run_cmd(args: List[str], *, capture: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, capture_output=capture, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"Command {' '.join(args)} failed with code {result.returncode}:\n{result.stderr or result.stdout}"
        )
    return result


def git_output(args: List[str]) -> str:
    return run_cmd(["git"] + args).stdout.strip()


def detect_branch() -> str:
    branch = git_output(["rev-parse", "--abbrev-ref", "HEAD"])
    if branch == "HEAD":
        raise SystemExit("Detached HEAD detected; pass --sha and --run-id explicitly.")
    return branch


def detect_sha() -> str:
    return git_output(["rev-parse", "HEAD"])


def status_matches(entry: Dict[str, Any], desired_status: Optional[str]) -> bool:
    if not desired_status:
        return True
    return entry.get("conclusion") == desired_status


def normalize_run_entry(entry: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "id": entry.get("databaseId") or entry.get("id"),
        "sha": entry.get("headSha") or entry.get("head_sha"),
        "branch": entry.get("headBranch") or entry.get("head_branch"),
        "status": entry.get("status"),
        "conclusion": entry.get("conclusion"),
        "updated": entry.get("updatedAt") or entry.get("updated_at"),
    }


def load_runs_cli(
    gh: str,
    workflow: str,
    branch: str,
    limit: int,
) -> List[Dict[str, Any]]:
    args = [
        gh,
        "run",
        "list",
        "--workflow",
        workflow,
        "--branch",
        branch,
        "--limit",
        str(limit),
        "--json",
        "databaseId,headSha,headBranch,status,conclusion,updatedAt",
    ]
    result = run_cmd(args)
    return json.loads(result.stdout)


def matches_workflow(entry: Dict[str, Any], workflow: Optional[str]) -> bool:
    if not workflow:
        return True
    return entry.get("name") == workflow


def matches_branch(entry: Dict[str, Any], branch: Optional[str]) -> bool:
    if not branch:
        return True
    return (entry.get("headBranch") or entry.get("head_branch")) == branch


def filter_runs(runs: List[Dict[str, Any]], workflow: Optional[str], branch: Optional[str]) -> List[Dict[str, Any]]:
    return [entry for entry in runs if matches_workflow(entry, workflow) and matches_branch(entry, branch)]


def pick_run_id_cli(
    gh: str,
    workflow: str,
    branch: str,
    sha: str,
    limit: int,
    latest: bool,
    desired_status: Optional[str],
) -> Tuple[int, str]:
    runs = load_runs_cli(gh, workflow, branch, limit)
    for entry in filter_runs(runs, workflow, branch):
        if not status_matches(entry, desired_status):
            continue
        if latest:
            return int(entry["databaseId"]), entry.get("headSha", "")
        if entry.get("headSha") == sha:
            return int(entry["databaseId"]), entry.get("headSha", "")
    criteria = "latest run" if latest else f"head SHA {sha}"
    if desired_status:
        criteria += f" and status {desired_status}"
    raise SystemExit(
        f"Could not find a CI run for workflow '{workflow}' on branch '{branch}' matching {criteria}. "
        "Use --run-id to specify a run manually or increase --limit."
    )


def download_artifact(gh: str, run_id: int, artifact_name: str, download_dir: Path) -> Path:
    download_dir.mkdir(parents=True, exist_ok=True)
    args = [
        gh,
        "run",
        "download",
        str(run_id),
        "--name",
        artifact_name,
        "--dir",
        str(download_dir),
    ]
    run_cmd(args, capture=False)
    archive = download_dir / f"{artifact_name}.zip"
    if not archive.exists():
        matches = list(download_dir.glob("*.zip"))
        if len(matches) == 1:
            archive = matches[0]
        else:
            raise SystemExit(f"Artifact zip not found at {archive}; available zips: {[str(z) for z in matches]}")
    return archive


def api_request(url: str, token: str) -> Dict[str, Any]:
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "User-Agent": "nicloadoff-artifact-fetcher",
    }
    req = request.Request(url, headers=headers)
    with request.urlopen(req) as response:  # nosec B310 - controlled URL
        data = response.read()
    return json.loads(data.decode("utf-8"))


def api_download(url: str, token: str, out_path: Path) -> None:
    headers = {
        "Accept": "application/octet-stream",
        "Authorization": f"Bearer {token}",
        "User-Agent": "nicloadoff-artifact-fetcher",
    }
    req = request.Request(url, headers=headers)
    with request.urlopen(req) as response:  # nosec B310 - controlled URL
        out_path.write_bytes(response.read())


def parse_repo_slug(remote_url: str) -> Tuple[str, str]:
    remote_url = remote_url.strip()
    if remote_url.startswith("git@"):
        _, remainder = remote_url.split(":", 1)
    elif remote_url.startswith("https://") or remote_url.startswith("http://"):
        remainder = parse.urlparse(remote_url).path.lstrip("/")
    else:
        remainder = remote_url
    if remainder.endswith(".git"):
        remainder = remainder[:-4]
    owner, repo = remainder.split("/", 1)
    return owner, repo


def detect_repo_slug(explicit: Optional[str]) -> Tuple[str, str]:
    if explicit:
        owner, repo = explicit.split("/", 1)
        return owner, repo
    remote = git_output(["config", "--get", "remote.origin.url"])
    if not remote:
        raise SystemExit("Unable to detect repo slug; specify --repo owner/name explicitly.")
    return parse_repo_slug(remote)


def load_runs_api(owner: str, repo: str, token: str, limit: int) -> List[Dict[str, Any]]:
    base = f"https://api.github.com/repos/{owner}/{repo}/actions/runs?per_page={limit}"
    return api_request(base, token).get("workflow_runs", [])


def pick_run_id_api(
    owner: str,
    repo: str,
    workflow: str,
    branch: str,
    sha: str,
    limit: int,
    token: str,
    latest: bool,
    desired_status: Optional[str],
) -> Tuple[int, str]:
    runs = filter_runs(load_runs_api(owner, repo, token, limit), workflow, branch)
    for run in runs:
        if not status_matches(run, desired_status):
            continue
        if latest:
            return int(run["id"]), run.get("head_sha", "")
        if run.get("head_sha") == sha:
            return int(run["id"]), run.get("head_sha", "")
    criteria = "latest run"
    if not latest:
        criteria = f"head SHA {sha}"
    if desired_status:
        criteria += f" and status {desired_status}"
    raise SystemExit(
        f"[api] Could not find workflow '{workflow}' on branch '{branch}' matching {criteria}. "
        "Provide --run-id or relax the filters."
    )


def download_artifact_api(
    owner: str,
    repo: str,
    token: str,
    run_id: int,
    artifact_name: str,
    download_dir: Path,
) -> Path:
    url = f"https://api.github.com/repos/{owner}/{repo}/actions/runs/{run_id}/artifacts?per_page=100"
    artifacts = api_request(url, token).get("artifacts", [])
    match = next((a for a in artifacts if a.get("name") == artifact_name), None)
    if match is None:
        available = ", ".join(a.get("name", "<unnamed>") for a in artifacts)
        raise SystemExit(
            f"[api] Artifact '{artifact_name}' not found on run {run_id}. Available: {available or 'none'}."
        )
    archive_url = f"https://api.github.com/repos/{owner}/{repo}/actions/artifacts/{match['id']}/zip"
    download_dir.mkdir(parents=True, exist_ok=True)
    archive = download_dir / f"{artifact_name}.zip"
    api_download(archive_url, token, archive)
    return archive


def print_run_table(entries: List[Dict[str, Any]], desired_status: Optional[str]) -> None:
    filtered = []
    for entry in entries:
        if status_matches(entry, desired_status):
            filtered.append(normalize_run_entry(entry))
    if not filtered:
        print("[fetch] No workflow runs matched the requested filters.")
        return
    print(f"{'RUN ID':>8}  {'STATUS':<10}  {'CONCLUSION':<10}  {'BRANCH':<15}  {'SHA':<8}  UPDATED")
    for entry in filtered:
        run_id = str(entry.get("id", "-"))
        status = entry.get("status", "") or "-"
        conclusion = entry.get("conclusion", "") or "-"
        branch = entry.get("branch", "") or "-"
        sha = (entry.get("sha") or "")[:8]
        updated = entry.get("updated", "") or "-"
        print(f"{run_id:>8}  {status:<10}  {conclusion:<10}  {branch:<15}  {sha:<8}  {updated}")


def unpack(archive: Path, extract_dir: Path) -> Path:
    extract_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(extract_dir)
    return extract_dir


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--workflow",
        default="CI",
        help="Workflow name to search (default: %(default)s; must match the Actions job name)",
    )
    parser.add_argument(
        "--artifact-name",
        default=None,
        help="Exact artifact name (default: policy-analysis-<sha>)",
    )
    parser.add_argument(
        "--branch",
        default=None,
        help="Branch to search runs for (default: current branch)",
    )
    parser.add_argument(
        "--sha",
        default=None,
        help="Commit SHA to match (default: HEAD)",
    )
    parser.add_argument(
        "--run-id",
        type=int,
        default=None,
        help="Override the workflow run ID (skips auto-detection)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Number of recent runs to inspect when auto-selecting (default: %(default)s)",
    )
    parser.add_argument(
        "--gh",
        default="gh",
        help="Path to the GitHub CLI executable (default: %(default)s)",
    )
    parser.add_argument(
        "--use-api",
        action="store_true",
        help="Force use of the GitHub REST API even if the gh CLI is available",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List recent workflow runs instead of downloading artifacts",
    )
    parser.add_argument(
        "--latest",
        action="store_true",
        help="Download the most recent run (ignores head SHA match, still filtered by branch/workflow)",
    )
    parser.add_argument(
        "--status",
        default=None,
        help="Filter runs by conclusion (e.g., success, failure). Default: no filter.",
    )
    parser.add_argument(
        "--repo",
        default=None,
        help="Explicit owner/repo slug (default: auto-detect from git remote)",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="GitHub token for API fallback (default: read from GITHUB_TOKEN env var)",
    )
    parser.add_argument(
        "--download-dir",
        type=Path,
        default=Path("artifacts/policy-analysis"),
        help="Directory for downloaded archives (default: %(default)s)",
    )
    parser.add_argument(
        "--extract-dir",
        type=Path,
        default=Path("artifacts/policy-analysis/extracted"),
        help="Directory to extract artifacts into (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sha = args.sha or detect_sha()
    branch = args.branch or detect_branch()
    repo_owner = repo_name = None
    token: Optional[str] = args.token or os.environ.get("GITHUB_TOKEN")
    have_cli = shutil.which(args.gh) is not None
    if args.list:
        if have_cli and not args.use_api:
            runs = load_runs_cli(args.gh, args.workflow, branch, args.limit)
        else:
            repo_owner, repo_name = detect_repo_slug(args.repo)
            if not token:
                raise SystemExit(
                    "GitHub CLI not available (or --use-api set) and no token found. "
                    "Set the GITHUB_TOKEN environment variable or pass --token."
                )
            runs = load_runs_api(repo_owner, repo_name, token, args.limit)
        runs = filter_runs(runs, args.workflow, branch)
        print_run_table(runs, args.status)
        return 0
    run_id = args.run_id
    selected_sha = sha
    if run_id is None:
        if have_cli and not args.use_api:
            run_id, selected_sha = pick_run_id_cli(
                args.gh, args.workflow, branch, sha, args.limit, args.latest, args.status
            )
            target_desc = f"{branch} (latest)" if args.latest else f"{branch}@{sha}"
            print(f"[fetch] Selected run ID {run_id} via gh for {args.workflow} on {target_desc}")
        else:
            repo_owner, repo_name = detect_repo_slug(args.repo)
            if not token:
                raise SystemExit(
                    "GitHub CLI not available (or --use-api set) and no token found. "
                    "Set the GITHUB_TOKEN environment variable or pass --token."
                )
            run_id, selected_sha = pick_run_id_api(
                repo_owner,
                repo_name,
                args.workflow,
                branch,
                sha,
                args.limit,
                token,
                args.latest,
                args.status,
            )
            target_desc = f"{branch} (latest)" if args.latest else f"{branch}@{sha}"
            print(f"[fetch] Selected run ID {run_id} via REST API for {args.workflow} on {target_desc}")
    artifact_name = args.artifact_name or f"policy-analysis-{selected_sha or sha}"
    if have_cli and not args.use_api:
        archive = download_artifact(args.gh, run_id, artifact_name, args.download_dir)
    else:
        if repo_owner is None or repo_name is None:
            repo_owner, repo_name = detect_repo_slug(args.repo)
        if not token:
            raise SystemExit(
                "GitHub CLI not available (or disabled) and no token provided. "
                "Set GITHUB_TOKEN or pass --token."
            )
        archive = download_artifact_api(repo_owner, repo_name, token, run_id, artifact_name, args.download_dir)
    print(f"[fetch] Downloaded artifact archive to {archive}")
    extracted_path = unpack(archive, args.extract_dir)
    print(f"[fetch] Extracted files under {extracted_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
