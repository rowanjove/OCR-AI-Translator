"""Audit candidate or staged Git bytes without printing secret values (Python 3.8+)."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r"\b(?:sk-[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AIza[A-Za-z0-9_-]{30,}|AKIA[A-Z0-9]{16})\b")
PRIVATE_PATH = re.compile(r"(?i)[A-Z]:[\\/](?:Users[\\/]([^\\/\s\"']+)|AI[\\/]vibecoding)")
ASSIGNMENT = re.compile(r'''(?i)["'](?:api[_-]?key|access_token|refresh_token|client_secret)["']\s*:\s*["']([^"'\r\n]+)["']''')
SENSITIVE_NAME = re.compile(r"(?i)(?:^|/)(?:\.env(?:\..*)?|config\.json(?:\..*)?|secret-.*\.bin)$|\.(?:pem|pfx|p12|key|log|pdb|exe|zip|pyc)$")
FORBIDDEN_ROOTS = {"build", "out", "release", "docs/evidence", "docs/ui"}
ALLOWED_BINARY = {".png", ".ico"}


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def scan_text(name, data):
    rows = []
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return [{"path": name, "rule": "unexpected_binary_or_encoding"}]
    patterns = [("provider_token", TOKEN), ("credential_literal", ASSIGNMENT),
                ("private_key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----")),
                ("private_absolute_path", PRIVATE_PATH)]
    for rule, pattern in patterns:
        for match in pattern.finditer(text):
            # Deliberate synthetic WindowsApps rejection fixture, not a user's path.
            if rule == "private_absolute_path" and match.group(1) == "test" and name in {"tests/core_tests.cpp", "tests/process_tests.cpp"}:
                continue
            value = match.group(0)
            rows.append({"path": name, "line": text.count("\n", 0, match.start()) + 1,
                         "rule": rule, "match_sha256": hashlib.sha256(value.encode()).hexdigest()})
    if name.endswith(".json"):
        try:
            obj = json.loads(text)
        except ValueError:
            return rows + [{"path": name, "rule": "invalid_json"}]

        def walk(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    lower = key.lower()
                    if lower in {"credential_ref", "api_key", "apikey", "token", "authorization", "access_token", "refresh_token", "client_secret"} and child:
                        rows.append({"path": name, "rule": "nonempty_credential_field"})
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(obj)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--staged", action="store_true", help="Read exact index bytes, not working copies")
    mode.add_argument("--tracked", action="store_true", help="Read tracked working files only (CI)")
    parser.add_argument("--output", help="Optional local JSON evidence path")
    args = parser.parse_args()
    cmd = ["ls-files", "-z", "--cached"]
    if not (args.staged or args.tracked):
        cmd += ["--others", "--exclude-standard"]
    names = sorted(set(x.decode("utf-8") for x in git(*cmd).split(b"\0") if x))
    failures = []
    inventory = []
    for name in names:
        if any(name == p or name.startswith(p + "/") for p in FORBIDDEN_ROOTS) or SENSITIVE_NAME.search(name):
            failures.append({"path": name, "rule": "forbidden_public_path"})
        path = ROOT / name
        mode_info = git("ls-files", "--stage", "--", name).decode("utf-8").split()
        if path.is_symlink() or (mode_info and mode_info[0] in {"120000", "160000"}):
            failures.append({"path": name, "rule": "symlink_or_submodule"})
            continue
        try:
            data = git("show", ":" + name) if args.staged else path.read_bytes()
        except (OSError, subprocess.CalledProcessError):
            failures.append({"path": name, "rule": "unreadable"})
            continue
        inventory.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        if path.suffix.lower() not in ALLOWED_BINARY:
            failures.extend(scan_text(name, data))
    # Audit every publishable head/tag/remote plus HEAD. Codex keeps private
    # worktree snapshots under refs/codex; those are not pushed or published.
    # Keep these snapshots intact, but do not treat them as public history.
    history_blobs = 0
    if subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "HEAD"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        for line in git("rev-list", "--objects", "HEAD", "--branches", "--tags", "--remotes").decode("utf-8").splitlines():
            oid, _, name = line.partition(" ")
            if git("cat-file", "-t", oid).strip() != b"blob":
                continue
            history_blobs += 1
            if SENSITIVE_NAME.search(name):
                failures.append({"path": name, "rule": "forbidden_historical_path", "blob": oid})
            if Path(name).suffix.lower() not in ALLOWED_BINARY:
                for row in scan_text(name, git("cat-file", "blob", oid)):
                    row["blob"] = oid
                    failures.append(row)
    report = {"mode": "index" if args.staged else "tracked" if args.tracked else "candidate",
              "files": inventory, "file_count": len(inventory), "history_blobs_scanned": history_blobs,
              "findings": failures, "result": "FAIL" if failures else "PASS",
              "limits": "Heuristic text scan of candidate files and HEAD/branches/tags/remotes history; private app checkpoint refs are not publication refs. Images require visual review; binary source assets are not decoded for hidden data. No external user config is read."}
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ["mode", "file_count", "history_blobs_scanned", "result", "findings"]}, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
